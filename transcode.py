import argparse
import subprocess
import sys


def parse_args():
    parser = argparse.ArgumentParser(description="Video transcode automation script.")
    parser.add_argument('--compile', action='store_true', help='Recompile the underlying C++ program')
    parser.add_argument('--gop', type=int, help='GOP size of the video transcode')
    parser.add_argument('--bitrate', type=int, help='Bitrate of the video transcode')
    parser.add_argument('--width', type=int, required=False, help='Width of the transcode resolution')
    parser.add_argument('--height', type=int, required=False, help='Height of the transcode resolution')
    parser.add_argument('--out', type=str, default='vid1.mp4', help='Output mp4 file (default: vid.mp4)')
    return parser.parse_args()


def main():
    args = parse_args()

    if args.compile:
        print('Compiling transcode.cpp...')
        compile_cmd = ['cl', 'transcode.cpp', '/Zi', '/EHsc']
        result = subprocess.run(compile_cmd)
        if result.returncode != 0:
            print('Compilation failed.')
            sys.exit(1)

    print('Running transcode.exe...')
    transcode_cmd = ['./transcode.exe']
    if args.gop is not None:
        transcode_cmd += ['--gop', str(args.gop)]
    if args.bitrate is not None:
        transcode_cmd += ['--bitrate', str(args.bitrate)]
    transcode_cmd += ['--width', str(args.width), '--height', str(args.height)]
    result = subprocess.run(transcode_cmd)
    if result.returncode != 0:
        print('transcode.exe failed.')
        sys.exit(1)

    print(f'Running ffmpeg to mux output to {args.out}...')
    ffmpeg_cmd = ['ffmpeg', '-i', 'vid1.h264', '-c', 'copy', args.out]
    result = subprocess.run(ffmpeg_cmd)
    if result.returncode != 0:
        print('ffmpeg failed.')
        sys.exit(1)
    print('Done.')


if __name__ == '__main__':
    main()
