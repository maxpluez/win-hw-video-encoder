import argparse
import subprocess
import sys

import platform
import os
import GPUtil
import csv
import json
import statistics
import math

import boto3
from botocore.exceptions import ClientError

from google_auth_oauthlib.flow import InstalledAppFlow
from googleapiclient.discovery import build
import os

GSUN = 0.07

def generate_video_filename(hardware, gop, bitrate, mode, quality, codec, profile):
    """
    Generate output video file name in the format:
    "sonic720p-g<gop>-<bitrate>-<mode>-<codec>-<profile>.mp4"
    """
    return f"sonic720p-{'hw' if hardware else 'sw'}-g{gop}-{bitrate}-{mode}{str(quality) if mode == 'quality' else ''}-{codec}-{profile}.mp4"

def generate_probe_filename(video_filename):
    """
    Generate output probe file name in the format:
    "<video_filename>.json"
    """
    return f"{video_filename}.json"

def generate_vmaf_filename(video_filename):
    """
    Generate output VMAF file name in the format:
    "<video_filename>.vmaf.json"
    """
    return f"{video_filename}.vmaf.json"

def generate_device_id():
    """
    Generate a device ID string in the format:
    "device id-device name-gpu name-hardware or software"
    using OS information and GPUtil for GPU name.
    """
    # Device ID: use machine node or hostname
    device_id = platform.node() or os.environ.get('COMPUTERNAME', 'unknown')
    # GPU name
    gpu_name = 'UnknownGPU'
    result = subprocess.run(
        ["powershell", "Get-CimInstance -ClassName Win32_VideoController | Select-Object -Property Name"],
        capture_output=True,
        text=True,
        check=True
    )
    if result.returncode == 0:
        gpu_name = result.stdout.strip().splitlines()[2] if len(result.stdout.strip().splitlines()) > 2 else 'UnknownGPU'
    device_id_str = f"{device_id}-{gpu_name}"
    return device_id_str.replace(' ', '_')

def upload_to_s3(file, device_id, s3_file_name):
    print("Uploading {} to S3 with file name {}...".format(file, s3_file_name))
    object_name = "videos/video-quality/" + device_id + "/" + s3_file_name

    # Upload the file
    s3_client = boto3.client('s3')
    try:
        response = s3_client.upload_file(file, 'audiovisual-test-public', object_name, ExtraArgs={'ACL': 'public-read'})
        print("Upload succeeded with response:", response)
    except ClientError as e:
        print("Upload failed with error:", e)
        return False
    return True

def parse_args():
    parser = argparse.ArgumentParser(description="Video transcode automation script.")
    parser.add_argument('--compile', type=bool, default=False, help='Recompile the underlying C++ program')
    parser.add_argument('--gop', type=int, default=30, help='GOP size of the video transcode')
    parser.add_argument('--bitrate', type=float, default=1, help='Bitrate of the video transcode, in gsuns')
    parser.add_argument('--width', type=int, default=1568, help='Width of the transcode resolution')
    parser.add_argument('--height', type=int, default=720, help='Height of the transcode resolution')
    parser.add_argument('--hardware', type=bool, default=True, help='Hardware or software encoder')
    parser.add_argument('--mode', type=str, default='cbr', help='Encoding mode (default: cbr)')
    parser.add_argument('--codec', type=str, default='h264', help='Codec to use (default: h264)')
    parser.add_argument('--framerate', type=int, default=30, help='Frame rate of the video transcode (default: 30)')
    parser.add_argument('--sso', type=bool, default=False, help='Use AWS SSO to login before uploading to S3')
    return parser.parse_args()

def fetch_google_doc_content():
    doc_id = os.environ.get('VIDEO_AWS_TOKEN_DOC_ID')
    if not doc_id:
        raise ValueError("VIDEO_AWS_TOKEN_DOC_ID environment variable not set.")

    # Path to your OAuth client ID file
    CLIENT_SECRET_FILE = 'credentials.json'
    SCOPES = ['https://www.googleapis.com/auth/documents.readonly']

    # Run local server flow to get credentials
    flow = InstalledAppFlow.from_client_secrets_file(CLIENT_SECRET_FILE, SCOPES)
    creds = flow.run_local_server(port=0)

    service = build('docs', 'v1', credentials=creds)
    doc = service.documents().get(documentId=doc_id).execute()

    # Extract text content from the document
    content = []
    for element in doc.get('body', {}).get('content', []):
        if 'paragraph' in element:
            for p_element in element['paragraph'].get('elements', []):
                text_run = p_element.get('textRun')
                if text_run:
                    content.append(text_run.get('content', ''))
    return ''.join(content)

def fetch_google_doc_tokens():
    content = fetch_google_doc_content()
    aws_access_key_id = None
    aws_secret_access_key = None
    aws_session_token = None
    for line in content.splitlines():
        if line.startswith('aws_access_key_id='):
            aws_access_key_id = line.split('=', 1)[1].strip()
            os.environ['AWS_ACCESS_KEY_ID'] = aws_access_key_id
        elif line.startswith('aws_secret_access_key='):
            aws_secret_access_key = line.split('=', 1)[1].strip()
            os.environ['AWS_SECRET_ACCESS_KEY'] = aws_secret_access_key
        elif line.startswith('aws_session_token='):
            aws_session_token = line.split('=', 1)[1].strip()
            os.environ['AWS_SESSION_TOKEN'] = aws_session_token
    return aws_access_key_id, aws_secret_access_key, aws_session_token

def main():
    args = parse_args()

    fetch_google_doc_tokens();
    device_id = generate_device_id()

    if (args.sso):
        aws_sso_cmd = ['aws', 'sso', 'login', '--profile', 'test-audiovisual']
        result = subprocess.run(aws_sso_cmd)
        if result.returncode != 0:
            print('AWS SSO login failed. Will use existing AWS tokens in the environment.')

    if args.compile:
        print('Compiling transcode.cpp...')
        compile_cmd = ['cl', 'transcode.cpp', '/Zi', '/EHsc']
        result = subprocess.run(compile_cmd)
        if result.returncode != 0:
            print('Compilation failed. Using the existing transcode.exe.')
    
    bitratesGsun = [1.0, 1.5, 2.0, 2.5, 3.0] # [1.0, 1.5, 2.0, 2.5, 10.0]
    modes = ['cbr', 'vbr', 'quality'] # ['cbr', 'vbr', 'quality', 'fast']
    hws = [True, False] # [True, False]
    qualities = [0, 50, 100] # [0, 50, 100]
    gops = [30, 90, 180] # [30, 90, 180]
    profiles = ["baseline", "main", "high"] # ["baseline", "main", "high", "constrained", "simple"]
    codecs = ["h264", "hevc"]

    #get all configurations to run
    configs = []
    # [1.0, 1.5, 2.0, 2.5, 10.0]
    for bitrateGsun in bitratesGsun:
        bitrate = int(float(args.width) * float(args.height) * float(args.framerate) * bitrateGsun * GSUN)
        for codec in codecs:
            for mode in modes:
                curr_qualities = qualities if mode == 'quality' else [100]
                for hw in hws:
                    for quality in curr_qualities:
                        for gop in gops:
                            for profile in profiles:
                                configs.append({ 'bitrate' : bitrate, 'hardware' : hw, 'mode': mode, 'quality': quality, 'gop': gop, 'profile': profile, 'codec': codec })

    # Create and write header to csv
    csv_file = 'windows_quality.csv'
    csv_header = [
        'link', 'video id', 'device id', 'gpu', 'encoder', 'codec', 'mode', 'gop', 'fps',
        'requested_bitrate', 'actual_bitrate', 'profile', 'vmaf_hmean', 'vmaf_stddev', 'bpb', 'time_ms'
    ]
    with open(csv_file, 'w', newline='') as f:
        writer = csv.writer(f)
        writer.writerow(csv_header)

    for config in configs:
        print("Running transcode with config: " + str(config))
        print('Running transcode.exe...')
        transcode_cmd = ['./transcode.exe']
        #if args.gop is not None:
        #    transcode_cmd += ['--gop', str(args.gop)]
        if config['bitrate'] is not None and config['bitrate'] > 0:
            transcode_cmd += ['--bitrate', str(config['bitrate'])]
        if config['hardware'] is not None:
            transcode_cmd += ['--hardware', str(config['hardware']).lower()]
        if config['mode'] is not None:
            transcode_cmd += ['--mode', str(config['mode'])]
        if config['quality'] is not None and config['mode'] == 'quality':
            transcode_cmd += ['--quality', str(config['quality'])]
        if config['gop'] is not None:
            transcode_cmd += ['--gop', str(config['gop'])]
        if config['profile'] is not None:
            transcode_cmd += ['--profile', str(config['profile'])]
        if config['codec'] is not None:
            transcode_cmd += ['--codec', str(config['codec'])]
        #transcode_cmd += ['--width', str(args.width), '--height', str(args.height)]
        # Run transcode.exe and capture output
        result = subprocess.run(transcode_cmd, capture_output=True, text=True)
        if result.returncode != 0:
            print('transcode.exe failed. Write a row in csv and continuing to the next config.')
            csv_row = [
                "",
                "",
                device_id,
                gpu_name,
                'Hardware' if config['hardware'] else 'Software',
                config['codec'],
                config['mode'] + ('' if config['mode'] != 'quality' else str(config['quality'])),
                config['gop'],
                args.framerate,
                config['bitrate'],
                0,
                config['profile'],
                0,
                0,
                0,
                0
            ]
            with open(csv_file, 'a', newline='') as f:
                writer = csv.writer(f)
                writer.writerow(csv_row)
            continue
        # Parse encoding time from output
        encoding_time_ms = None
        for line in result.stdout.splitlines():
            if line.startswith('Encoding time:'):
                try:
                    encoding_time_ms = float(line.split(':')[1].strip().split()[0])
                except Exception:
                    encoding_time_ms = None
                break

        print(f'Running ffmpeg to mux output to vid.mp4...')
        raw_vid = f'vid.{'h265' if (config['codec'] is not None and (config['codec'] == 'hevc' or config['codec'] == 'h265')) else 'h264' }'
        ffmpeg_cmd = ['ffmpeg', '-r', '30', '-i', raw_vid, '-c', 'copy', '-y', 'vid.mp4']
        result = subprocess.run(ffmpeg_cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if result.returncode != 0:
            print('ffmpeg failed.')
            sys.exit(1)

        print(f'Running ffprobe...')
        ffprobe_cmd = ['ffprobe', '-print_format', 'json', '-show_frames', '-show_streams', 'vid.mp4']
        with open('probe.json', 'w') as f:
            result = subprocess.run(ffprobe_cmd, stdout=f, stderr=subprocess.DEVNULL)
            if result.returncode != 0:
                print('ffprobe failed.')
                sys.exit(1)

        print(f'Running vmaf...')
        vmaf_command = [
            'ffmpeg',
            '-r', '30',
            '-i', 'sonic720p.y4m',
            '-r', '30',
            '-i', 'vid.mp4',
            '-lavfi', f"[0:v]setpts=PTS-STARTPTS[reference];[1:v]setpts=PTS-STARTPTS[distorted];[distorted][reference]libvmaf=log_fmt=json:log_path=vmaf.json:n_threads=4",
            '-f', 'null',
            '-'
        ]
        result = subprocess.run(vmaf_command, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if result.returncode != 0:
            print('ffprobe failed.')
            sys.exit(1)

        # --- CSV WRITING LOGIC ---
        # Read VMAF and probe data
        with open('vmaf.json', 'r') as f:
            vmaf_data = json.load(f)
        with open('probe.json', 'r') as f:
            probe_data = json.load(f)

        # Find video stream and get actual bitrate
        actual_bitrate = None
        for stream in probe_data.get('streams', []):
            if stream.get('codec_type') == 'video':
                actual_bitrate = stream.get('bit_rate')
                break

        # Extract required data
        vmaf_harmonic_mean = vmaf_data["pooled_metrics"]["vmaf"]["harmonic_mean"]
        vmafs = [x["metrics"]["vmaf"] for x in vmaf_data["frames"]]
        vmaf_std_dev = statistics.stdev(vmafs)
        bpb = vmaf_harmonic_mean / math.log2(int(actual_bitrate))

        # Get GPU name from device_id string
        gpu_name = device_id.split('-')[-2] if 'unknown' not in device_id.lower() else 'UnknownGPU'

        # Construct S3 link
        video_s3_file_name = generate_video_filename(config['hardware'], config['gop'], config['bitrate'], config['mode'], config['quality'], config['codec'], config['profile'])
        s3_link = f"https://audiovisual-test-public.s3.us-east-1.amazonaws.com/videos/video-quality/{device_id}/{video_s3_file_name}"

        # Prepare data row
        csv_row = [
            s3_link,
            device_id + "/" + video_s3_file_name,
            device_id,
            gpu_name,
            'Hardware' if config['hardware'] else 'Software',
            config['codec'],
            config['mode'] + ('' if config['mode'] != 'quality' else str(config['quality'])),
            config['gop'],
            args.framerate,
            config['bitrate'],
            actual_bitrate,
            config['profile'],
            vmaf_harmonic_mean,
            vmaf_std_dev,
            bpb,
            str(encoding_time_ms) if encoding_time_ms is not None else 'N/A'
        ]

        # Append row to CSV
        with open(csv_file, 'a', newline='') as f:
            writer = csv.writer(f)
            writer.writerow(csv_row)
        # --- END CSV WRITING LOGIC ---

        upload_to_s3('vid.mp4', device_id, video_s3_file_name)
        upload_to_s3('probe.json', device_id, generate_probe_filename(video_s3_file_name))
        upload_to_s3('vmaf.json', device_id, generate_vmaf_filename(video_s3_file_name))

    os.remove('vid.h264')
    os.remove('vid.mp4')
    os.remove('probe.json')
    os.remove('vmaf.json')
    print('Done.')

if __name__ == '__main__':
    main()
