gcc -shared -o libffmpeg-zya2.so \
    -Wl,-Bsymbolic \
    -Wl,--whole-archive \
    libavcodec.a \
    libavdevice.a \
    libavfilter.a \
    libavformat.a \
    libavutil.a \
    libpostproc.a \
    libswresample.a \
    libswscale.a \
    -Wl,--no-whole-archive \
    -fPIC \
    -lSDL2 \
    -lX11 -lXext -lXv -lxcb \
    -lx264 -lx265 \
    -lasound \
    -lsndio \
    -lm -lz -lpthread -ldl