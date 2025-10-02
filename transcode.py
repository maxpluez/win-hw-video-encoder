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

import functools
import multiprocessing
import time


GSUN = 0.07

def generate_video_filename_no_ext(height, hardware, gop, bitrate, mode, quality, codec, profile):
    return f"sonic{height}p-{'hw' if hardware else 'sw'}-g{gop}-{bitrate}-{mode}{str(quality) if mode == 'quality' else ''}-{codec}-{profile}"

def generate_video_filename(video_filename_no_ext):
    """
    Generate output video file name in the format:
    "sonic720p-g<gop>-<bitrate>-<mode>-<codec>-<profile>.mp4"
    """
    return f"{video_filename_no_ext}.mp4"

def generate_compressed_video_filename(video_filename_no_ext, codec):
    return f"{video_filename_no_ext}.{codec}"

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

def generate_gpu_name():
    gpu_name = 'UnknownGPU'
    result = subprocess.run(
        ["powershell", "Get-CimInstance -ClassName Win32_VideoController | Select-Object -Property Name"],
        capture_output=True,
        text=True,
        check=True
    )
    if result.returncode == 0:
        gpu_name = result.stdout.strip().splitlines()[2] if len(result.stdout.strip().splitlines()) > 2 else 'UnknownGPU'
    return gpu_name.replace(' ', '_')

def generate_device_id(gpu_name):
    """
    Generate a device ID string in the format:
    "device id-device name-gpu name-hardware or software"
    using OS information and GPUtil for GPU name.
    """
    # Device ID: use machine node or hostname
    device_id = platform.node() or os.environ.get('COMPUTERNAME', 'unknown')
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
    parser.add_argument('--compile', action=argparse.BooleanOptionalAction, default=False, help='Recompile the underlying C++ program')
    parser.add_argument('--gop', type=int, default=30, help='GOP size of the video transcode')
    parser.add_argument('--bitrate', type=float, default=1, help='Bitrate of the video transcode, in gsuns')
    parser.add_argument('--width', type=int, default=1568, help='Width of the transcode resolution')
    parser.add_argument('--height', type=int, default=720, help='Height of the transcode resolution')
    parser.add_argument('--hardware', action=argparse.BooleanOptionalAction, default=True, help='Hardware or software encoder')
    parser.add_argument('--mode', type=str, default='cbr', help='Encoding mode (default: cbr)')
    parser.add_argument('--codec', type=str, default='h264', help='Codec to use (default: h264)')
    parser.add_argument('--framerate', type=int, default=30, help='Frame rate of the video transcode (default: 30)')
    parser.add_argument('--sso', action=argparse.BooleanOptionalAction, default=False, help='Use AWS SSO to login before uploading to S3')
    parser.add_argument('--parallel', action=argparse.BooleanOptionalAction, default=True, help='Run configurations in parallel.')
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

def process_config(config, args, device_id, gpu_name):
    """Processes a single video transcoding configuration."""
    try:
        video_file_name_no_ext = generate_video_filename_no_ext(config['height'], config['hardware'], config['gop'], config['bitrate'], config['mode'], config['quality'], config['codec'], config['profile'])
        video_s3_file_name = generate_video_filename(video_file_name_no_ext)
        compressed_video_file_name = generate_compressed_video_filename(video_file_name_no_ext, config['codec'])
        probe_file_name = generate_probe_filename(video_s3_file_name)
        vmaf_file_name = generate_vmaf_filename(video_s3_file_name)

        print("Running transcode with config: " + str(config))
        print('Running transcode.exe...')
        transcode_cmd = ['./transcode.exe']
        transcode_cmd += ['--out', video_file_name_no_ext]
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
        if config['width'] is not None:
            transcode_cmd += ['--width', str(config['width'])]
        if config['height'] is not None:
            transcode_cmd += ['--height', str(config['height'])]
        
        result = subprocess.run(transcode_cmd, capture_output=True, text=True)
        if result.returncode != 0:
            print(f"transcode.exe failed for {config}. Error: {result.stderr}")
            return [
                "", "", device_id, gpu_name, config['height'], 'Hardware' if config['hardware'] else 'Software',
                config['codec'], config['mode'] + ('' if config['mode'] != 'quality' else str(config['quality'])),
                config['gop'], args.framerate, config['bitrate'], 0, config['profile'], 0, 0, 0, 0
            ]

        encoding_time_ms = None
        for line in result.stdout.splitlines():
            if line.startswith('Encoding time:'):
                try:
                    encoding_time_ms = float(line.split(':')[1].strip().split()[0])
                except Exception:
                    encoding_time_ms = None
                break

        print(f'Running ffmpeg to mux output to mp4 for {config}')
        ffmpeg_cmd = ['ffmpeg', '-r', '30', '-i', compressed_video_file_name, '-c', 'copy', '-y', video_s3_file_name]
        result = subprocess.run(ffmpeg_cmd, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
        if result.returncode != 0:
            print(f'ffmpeg failed for {config}. Error: {result.stderr}')
            return None

        print(f'Running ffprobe for {config}')
        ffprobe_cmd = ['ffprobe', '-print_format', 'json', '-show_frames', '-show_streams', video_s3_file_name]
        with open(probe_file_name, 'w') as f:
            result = subprocess.run(ffprobe_cmd, stdout=f, stderr=subprocess.PIPE, text=True)
            if result.returncode != 0:
                print(f'ffprobe failed for {config}. Error: {result.stderr}')
                return None

        print(f'Running vmaf for {config}')
        vmaf_command = [
            'ffmpeg', '-r', '30', '-i', 'sonic1080p.y4m', '-r', '30', '-i', video_s3_file_name,
            '-lavfi', f"[0:v]settb=AVTB,setpts=PTS-STARTPTS,fps=30,scale={config['width']}:{config['height']}:flags=bicubic[reference];[1:v]settb=AVTB,setpts=PTS-STARTPTS,fps=30,scale={config['width']}:{config['height']}:flags=bicubic[distorted];[distorted][reference]libvmaf=log_fmt=json:log_path={vmaf_file_name}:n_threads=4",
            '-f', 'null', '-'
        ]
        result = subprocess.run(vmaf_command, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
        if result.returncode != 0:
            print(f'vmaf failed for {config}. Error: {result.stderr}')
            return None

        with open(vmaf_file_name, 'r') as f:
            vmaf_data = json.load(f)
        with open(probe_file_name, 'r') as f:
            probe_data = json.load(f)

        actual_bitrate = None
        for stream in probe_data.get('streams', []):
            if stream.get('codec_type') == 'video':
                actual_bitrate = stream.get('bit_rate')
                break
        
        if not actual_bitrate:
            print(f"Could not find video stream or bitrate for {config}")
            return None

        vmaf_harmonic_mean = vmaf_data["pooled_metrics"]["vmaf"]["harmonic_mean"]
        vmafs = [x["metrics"]["vmaf"] for x in vmaf_data["frames"]]
        vmaf_std_dev = statistics.stdev(vmafs) if len(vmafs) > 1 else 0
        bpb = vmaf_harmonic_mean / math.log2(int(actual_bitrate)) if int(actual_bitrate) > 1 else 0

        s3_link = f"https://audiovisual-test-public.s3.us-east-1.amazonaws.com/videos/video-quality/{device_id}/{video_s3_file_name}"

        csv_row = [
            s3_link, device_id + "/" + video_s3_file_name, device_id, gpu_name, config['height'],
            'Hardware' if config['hardware'] else 'Software', config['codec'],
            config['mode'] + ('' if config['mode'] != 'quality' else str(config['quality'])),
            config['gop'], args.framerate, config['bitrate'], actual_bitrate, config['profile'],
            vmaf_harmonic_mean, vmaf_std_dev, bpb, str(encoding_time_ms) if encoding_time_ms is not None else 'N/A'
        ]

        upload_to_s3(video_s3_file_name, device_id, video_s3_file_name)
        upload_to_s3(probe_file_name, device_id, probe_file_name)
        upload_to_s3(vmaf_file_name, device_id, vmaf_file_name)

        return csv_row

    finally:
        # Cleanup generated files
        for f in [compressed_video_file_name, video_s3_file_name, probe_file_name, vmaf_file_name]:
            if os.path.exists(f):
                os.remove(f)

def main():
    args = parse_args()

    if (args.sso):
        aws_sso_cmd = ['aws', 'sso', 'login', '--profile', 'test-audiovisual']
        result = subprocess.run(aws_sso_cmd)
        if result.returncode != 0:
            print('AWS SSO login failed. Will use existing AWS tokens in the environment.')
    else:
        fetch_google_doc_tokens()

    start_time = time.time()

    gpu_name = generate_gpu_name()
    device_id = generate_device_id(gpu_name)


    if args.compile:
        print('Compiling transcode.cpp...')
        compile_cmd = ['cl', 'transcode.cpp', '/Zi', '/EHsc']
        result = subprocess.run(compile_cmd)
        if result.returncode != 0:
            print('Compilation failed. Using the existing transcode.exe.')
    
    resolutions = [[1568, 720], [2336, 1080]] # [[1568, 720], [2336, 1080]]
    bitratesGsun = [2.0, 3.0, 4.0] # [1.0, 1.5, 2.0, 2.5, 10.0]
    modes = ['cbr', 'quality'] # ['cbr', 'vbr', 'quality', 'fast']
    hws = [True, False] # [True, False]
    qualities = [10, 30] # [0, 50, 100]
    gops = [30, 180] # [30, 90, 180]
    profiles = ["baseline", "main", "high"] # ["baseline", "main", "high", "constrained", "simple"]
    codecs = ["h264", "hevc"] # ["h264", "hevc"]

    #get all configurations to run
    configs = []
    # [1.0, 1.5, 2.0, 2.5, 10.0]
    for resolution in resolutions:
        for bitrateGsun in bitratesGsun:
            bitrate = int(float(resolution[0]) * float(resolution[1]) * float(args.framerate) * bitrateGsun * GSUN)
            for codec in codecs:
                for mode in modes:
                    curr_qualities = qualities if mode == 'quality' else [100]
                    for hw in hws:
                        for quality in curr_qualities:
                            for gop in gops:
                                for profile in profiles:
                                    configs.append({ 'bitrate' : bitrate, 'hardware' : hw, 'mode': mode, 'quality': quality, 'gop': gop, 'profile': profile, 'codec': codec, 'width': resolution[0], 'height': resolution[1] })

    # Create and write header to csv
    csv_file = 'windows_quality.csv'
    csv_header = [
        'link', 'video id', 'device id', 'gpu', 'height', 'encoder', 'codec', 'mode', 'gop', 'fps',
        'requested_bitrate', 'actual_bitrate', 'profile', 'vmaf_hmean', 'vmaf_stddev', 'bpb', 'time_ms'
    ]
    with open(csv_file, 'w', newline='') as csv_f:
        writer = csv.writer(csv_f)
        writer.writerow(csv_header)

        if args.parallel:
            # Use multiprocessing to run configurations in parallel
            with multiprocessing.Pool(processes=multiprocessing.cpu_count()) as pool:
                # Create a partial function with fixed arguments
                process_func = functools.partial(process_config, args=args, device_id=device_id, gpu_name=gpu_name)
                
                # Map the function to the configs
                results = pool.map(process_func, configs)
                
                # Filter out None results and write to CSV
                for row in results:
                    if row:
                        writer.writerow(row)
        else:
            # Run configurations sequentially
            for config in configs:
                result = process_config(config, args, device_id, gpu_name)
                if result:
                    writer.writerow(result)

    end_time = time.time()
    print(f'Done in {end_time - start_time:.2f} seconds.')

if __name__ == '__main__':
    main()
