./configure \
  --enable-static \
  --disable-shared \
  --enable-pic \
  --prefix=/home/ubuntu2204/workspace/ffmpeg/zyalib \
  --enable-gpl \
  --enable-libx264 \
  --enable-libx265 \
  --enable-nonfree

make
make install