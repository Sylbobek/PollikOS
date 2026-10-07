demo.mp4 is an original generated test pattern and 440-Hz tone.
Recipe: tests/movie_native.py, FFmpeg lavfi testsrc2 160x96 at 12 fps for 2s, H264 Baseline/no B-frames/yuv420p and AAC-LC 48-kHz stereo.
Builds ship these fixed bytes; they do not transcode user media or run FFmpeg for guest playback.
