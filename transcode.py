import argparse
import subprocess
import sys

import platform
import os
import GPUtil
import csv
import json
import np
import math

import boto3
from botocore.exceptions import ClientError

GSUN = 0.07

def generate_video_filename(gop, bitrate, mode, codec):
    """
    Generate output video file name in the format:
    "sonic720p-g<gop>-<bitrate>-<mode>-<codec>.mp4"
    """
    return f"sonic720p-g{gop}-{bitrate}-{mode}-{codec}.mp4"

def generate_probe_filename(gop, bitrate, mode, codec):
    """
    Generate output probe file name in the format:
    "sonic720p-g<gop>-<bitrate>-<mode>-<codec>-probe.json"
    """
    return f"sonic720p-g{gop}-{bitrate}-{mode}-{codec}.mp4.json"

def generate_vmaf_filename(gop, bitrate, mode, codec):
    """
    Generate output VMAF file name in the format:
    "sonic720p-g<gop>-<bitrate>-<mode>-<codec>-vmaf.json"
    """
    return f"sonic720p-g{gop}-{bitrate}-{mode}-{codec}.mp4.vmaf.json"

def generate_device_id(usingHardware):
    """
    Generate a device ID string in the format:
    "device id-device name-gpu name-hardware or software"
    using OS information and GPUtil for GPU name.
    """
    # Device ID: use machine node or hostname
    device_id = platform.node() or os.environ.get('COMPUTERNAME', 'unknown')
    # Device name: use platform.system() + release
    device_name = f"{platform.system()} {platform.release()}"
    # GPU name: use GPUtil if available
    gpu_name = 'UnknownGPU'
    gpus = GPUtil.getGPUs()
    if gpus:
        gpu_name = gpus[0].name
    else:
        gpu_name = 'UnknownGPU'
    # Hardware or software: check for presence of GPU, else 'software'
    hw_or_sw = 'Hardware' if usingHardware else 'Software'
    device_id_str = f"{device_id}-{device_name}-{gpu_name}-{hw_or_sw}"
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
    parser.add_argument('--compile', type=bool, default=True, help='Recompile the underlying C++ program')
    parser.add_argument('--gop', type=int, default=30, help='GOP size of the video transcode')
    parser.add_argument('--bitrate', type=float, default=1, help='Bitrate of the video transcode, in gsuns')
    parser.add_argument('--width', type=int, default=1568, help='Width of the transcode resolution')
    parser.add_argument('--height', type=int, default=720, help='Height of the transcode resolution')
    parser.add_argument('--hardware', type=bool, default=True, help='Hardware or software encoder')
    parser.add_argument('--mode', type=str, default='cbr', help='Encoding mode (default: cbr)')
    parser.add_argument('--codec', type=str, default='h264', help='Codec to use (default: h264)')
    parser.add_argument('--framerate', type=int, default=30, help='Frame rate of the video transcode (default: 30)')
    return parser.parse_args()


def main():
    args = parse_args()

    # Example: print device id string
    device_id = generate_device_id(args.hardware)

    if args.compile:
        print('Compiling transcode.cpp...')
        compile_cmd = ['cl', 'transcode.cpp', '/Zi', '/EHsc']
        result = subprocess.run(compile_cmd)
        if result.returncode != 0:
            print('Compilation failed.')
            sys.exit(1)

    #calculate the bitrate in bps from gsuns
    configs = []
    for bitrateGsun in [1.0, 1.5, 2.0, 2.5]:
        bitrate = int(float(args.width) * float(args.height) * float(args.framerate) * bitrateGsun * GSUN)
        print("bitrate (bps): " + str(bitrate))
        configs.append({ 'bitrate' : bitrate })

    # Create and write header to csv
    csv_file = 'windows_quality.csv'
    csv_header = [
        'link', 'video id', 'device id', 'gpu', 'encoder', 'codec', 'mode', 'gop', 'fps',
        'requested_bitrate', 'actual_bitrate', 'profile', 'vmaf_hmean', 'vmaf_stddev', 'bpb'
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
        #transcode_cmd += ['--width', str(args.width), '--height', str(args.height)]
        result = subprocess.run(transcode_cmd)
        if result.returncode != 0:
            print('transcode.exe failed.')
            sys.exit(1)

        print(f'Running ffmpeg to mux output to vid.mp4...')
        ffmpeg_cmd = ['ffmpeg', '-r', '30', '-i', 'vid.h264', '-c', 'copy', '-y', 'vid.mp4']
        result = subprocess.run(ffmpeg_cmd)
        if result.returncode != 0:
            print('ffmpeg failed.')
            sys.exit(1)

        print(f'Running ffprobe...')
        ffprobe_cmd = ['ffprobe', '-print_format', 'json', '-show_frames', '-show_streams', 'vid.mp4']
        with open('probe.json', 'w') as f:
            result = subprocess.run(ffprobe_cmd, stdout=f)
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
        result = subprocess.run(vmaf_command)
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
        vmaf_std_dev = np.std(vmafs)
        bpb = vmaf_harmonic_mean / math.log2(int(actual_bitrate))

        # Get GPU name from device_id string
        gpu_name = device_id.split('-')[-2] if 'unknown' not in device_id.lower() else 'UnknownGPU'

        # Construct S3 link
        video_s3_file_name = generate_video_filename(args.gop, config['bitrate'], args.mode, args.codec)
        s3_link = f"https://audiovisual-test-public.s3.us-east-1.amazonaws.com/videos/video-quality/{device_id}/{video_s3_file_name}"

        # Prepare data row
        csv_row = [
            s3_link,
            device_id + "/" + video_s3_file_name,
            device_id,
            gpu_name,
            'Hardware' if args.hardware else 'Software',
            args.codec,
            args.mode,
            args.gop,
            args.framerate,
            config['bitrate'],
            actual_bitrate,
            'Main', # Profile
            vmaf_harmonic_mean,
            vmaf_std_dev,
            bpb
        ]

        # Append row to CSV
        with open(csv_file, 'a', newline='') as f:
            writer = csv.writer(f)
            writer.writerow(csv_row)
        # --- END CSV WRITING LOGIC ---

        upload_to_s3('vid.mp4', device_id, video_s3_file_name)
        upload_to_s3('probe.json', device_id, generate_probe_filename(args.gop, config['bitrate'], args.mode, args.codec))
        upload_to_s3('vmaf.json', device_id, generate_vmaf_filename(args.gop, config['bitrate'], args.mode, args.codec))

    os.remove('vid.h264')
    os.remove('vid.mp4')
    os.remove('probe.json')
    os.remove('vmaf.json')
    print('Done.')

if __name__ == '__main__':
    main()
