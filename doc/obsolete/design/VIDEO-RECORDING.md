# VIDEO RECORDING

Sure thing! Here is a structured Markdown document summarizing our technical discussion on building a low-overhead video and audio capture system for your ZX Spectrum emulator.

ZX Spectrum Emulator: Low-CPU Video & Audio Capture Guide

This document outlines an efficient architecture for capturing 320 \\times 256 gameplay and synchronized audio with minimal impact on the emulation loop.

1\. Video Implementation Strategy

To maintain low CPU usage, the emulator should avoid real-time H.264 encoding. Instead, it should act as a producer of raw frames, offloading the heavy lifting to a dedicated process or hardware encoder.

The FFmpeg Pipe Method

The most efficient approach is to pipe raw pixel data into FFmpeg using a background subprocess.

 \* Format: Raw RGB24 or YUV420P.

 \* Resolution: 320 \\times 256 (includes the border).

 \* Data Rate: Approx. 12 MB/s at 50 FPS—negligible for modern memory and I/O.

Recommended Command

ffmpeg -f rawvideo -pixel_format rgb24 -video_size 320x256 -framerate 50 -i pipe:0 -c:v libx264 -preset ultrafast -crf 18 output.mp4

Hardware Acceleration (GPU)

To reduce CPU load to nearly zero, utilize GPU-based encoders:

 \* NVIDIA: -c:v h264_nvenc

 \* Intel: -c:v h264_qsv

 \* macOS: -c:v h264_videotoolbox

2\. Audio Implementation Strategy

Audio should be treated identically to video: synthesized as raw PCM data and piped simultaneously to prevent processing overhead within the emulator.

Synchronization Logic

To prevent audio drift, the audio and video streams must be synchronized using a shared clock logic:

 \* Emulate: Process 20ms of machine state (one 50Hz frame).

 \* Video Dump: Write the 320 \\times 256 buffer to the video pipe.

 \* Audio Dump: Write exactly 882 samples (for 44.1kHz) or 960 samples (for 48kHz) to the audio pipe.

3\. Integrated Audio/Video Piping

By opening multiple file descriptors (pipes), you can feed both streams into a single FFmpeg instance for real-time muxing.

Multi-Stream Command

ffmpeg -f rawvideo -pixel_format rgb24 -video_size 320x256 -framerate 50 -i pipe:3 \\

       -f s16le -ar 44100 -ac 1 -i pipe:4 \\

       -c:v libx264 -preset ultrafast \\

       -c:a aac -b:a 128k \\

       -map 0:v -map 1:a output.mp4

 \* pipe:3: Video Input

 \* pipe:4: Audio Input (Signed 16-bit Little Endian)

4\. Alternative: The Post-Processing Workflow

If real-time piping causes micro-stuttering during emulation, use a two-step "offline" approach:

 \* Session Phase: Save raw data to temporary files.

   \* capture.yuv (Raw Video)

   \* capture.wav (Raw Audio)

 \* Encoding Phase: Once the emulator closes, trigger a final encode:

   ffmpeg -i capture.yuv -i capture.wav -c:v libx264 -c:a aac final_render.mp4

5\. Technical Tips for ZX Spectrum

 \* The Beeper: When synthesizing the 1-bit beeper, apply a simple low-pass filter or linear resampler to avoid harsh aliasing noise in the final recording.

 \* Border Updates: Only update the border pixels in your internal buffer when the ULA state changes to save internal memory bandwidth.