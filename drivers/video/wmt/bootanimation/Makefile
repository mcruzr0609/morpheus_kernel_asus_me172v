#
# This is a modified version of zlib, which does all memory
# allocation ahead of time.
#
# This is only the decompression, see zlib_deflate for the
# the compression
#
# Decompression needs to be serialized for each memory
# allocation.
#
# (The upsides of the simplification is that you can't get in
# any nasty situations wrt memory management, and that the
# uncompression can be done without blocking on allocation).
#
lzma_dec-objs := LzmaDec.o 

obj-$(CONFIG_LOGO_WMT_ANIMATION) += lzma_dec.o buffer.o animation.o


