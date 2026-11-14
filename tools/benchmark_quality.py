#!/usr/bin/env python3
"""Compare hardware encoding candidates on generated footage, without changing settings."""
import argparse
import json
import math
import pathlib
import re
import statistics
import subprocess
import time


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--ffmpeg", default="ffmpeg")
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--frames", type=int, default=180)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    source = "testsrc2=s=1024x1024:r=90,drawtext=text='Wyrm 0123456789 text clarity':fontsize=22:fontcolor=white:x=270:y=470"
    candidates = {"baseline": (0, ""), "quality1": (1, ""), "periphery": (0, "split[a][b];[b]gblur=sigma=1.2[low];[a]crop=512:512:256:256[center];[low][center]overlay=256:256,"), "known-bad": (0, "gblur=sigma=8,")}
    results = []
    for name, (quality, preprocess) in candidates.items():
        for trial in range(3):
            prefix = args.output / f"{name}-{trial}"
            encoded = prefix.with_suffix(".hevc")
            cmd = [args.ffmpeg, "-hide_banner", "-nostdin", "-benchmark_all", "-init_hw_device", "vulkan=vk:0", "-filter_hw_device", "vk", "-f", "lavfi", "-i", source,
                   "-vf", preprocess + "format=nv12,hwupload", "-c:v", "hevc_vulkan", "-bf", "0", "-async_depth", "1", "-tune", "ull", "-quality", str(quality), "-rc_mode", "cbr", "-b:v", "8M", "-frames:v", str(args.frames), "-f", "hevc", str(encoded)]
            start = time.monotonic()
            run = subprocess.run(cmd, capture_output=True, text=True, check=True)
            elapsed = time.monotonic() - start
            prefix.with_suffix(".encode.log").write_text(run.stderr)
            calls = sorted(int(x) for x in re.findall(r"(\d+) real encode_video", run.stderr))
            central = prefix.with_suffix(".center.psnr")
            whole = prefix.with_suffix(".whole.psnr")
            graph = f"[0:v]trim=end_frame={args.frames},settb=1/90,setpts=N,format=yuv420p,split[r][rc];[1:v]trim=end_frame={args.frames},settb=1/90,setpts=N,format=yuv420p,split[d][dc];[r][d]psnr=shortest=1:repeatlast=0:stats_file={whole}[all];[rc]crop=512:512:256:256[rcc];[dc]crop=512:512:256:256[dcc];[rcc][dcc]psnr=shortest=1:repeatlast=0:stats_file={central}[center]"
            measure = subprocess.run([args.ffmpeg, "-hide_banner", "-nostdin", "-f", "lavfi", "-i", source, "-i", str(encoded), "-filter_complex", graph, "-map", "[all]", "-map", "[center]", "-frames:v", str(args.frames), "-f", "null", "-"], capture_output=True, text=True, check=True)
            prefix.with_suffix(".measure.log").write_text(measure.stderr)
            scores = {}
            for region, file in (("center", central), ("whole", whole)):
                mse = [float(x) for x in re.findall(r"mse_avg:([\d.]+)", file.read_text())]
                if len(mse) != args.frames:
                    raise RuntimeError(f"{name}: {region} measured {len(mse)} frames, expected {args.frames}")
                scores[region + "_psnr_db"] = 10 * math.log10(255**2 / statistics.mean(mse))
            record = dict(candidate=name, trial=trial, frames=args.frames, bytes=encoded.stat().st_size, wall_seconds=elapsed, encode_call_p95_us=calls[int(len(calls) * .95)] if calls else None, **scores)
            results.append(record)
            print(json.dumps(record), flush=True)
            (args.output / "results.json").write_text(json.dumps(results, indent=2))
    baseline = statistics.median(x["center_psnr_db"] for x in results if x["candidate"] == "baseline")
    bad = statistics.median(x["center_psnr_db"] for x in results if x["candidate"] == "known-bad")
    if baseline - bad < 3:
        raise RuntimeError("Image quality measurement failed to reject the blurred negative control")


if __name__ == "__main__":
    main()
