# Cross-compile RTSP server for Anyka AK3918EV330
# Links statically against SDK libs not on device, dynamically against device's libs

CROSS   = arm-anykav200-linux-uclibcgnueabi-
CC      = $(CROSS)gcc
STRIP   = $(CROSS)strip

SDK     = /sdk

# Compiler flags
CFLAGS  = -Os -Wall -std=c99 -D_POSIX_C_SOURCE=199309L \
          -I$(SDK)/platform/libplat/include \
          -I$(SDK)/platform/libmpi/include \
          -I$(SDK)/platform/libapp/include

# Library paths
PLAT_LIB = $(SDK)/platform/libplat/lib
MPI_LIB  = $(SDK)/platform/libmpi/lib
APP_LIB  = $(SDK)/platform/libapp/lib

# Static libraries (not on device) - linked directly as .a files
STATIC_LIBS = \
    $(APP_LIB)/libapp_rtsp.a \
    $(APP_LIB)/libapp_net.a \
    $(PLAT_LIB)/libplat_ipcsrv.a \
    $(PLAT_LIB)/libplat_venc_cb.a \
    $(PLAT_LIB)/libakuio.a \
    $(PLAT_LIB)/libplat_drv.a \
    $(MPI_LIB)/libakstreamenc.a

# Dynamic libraries (exist in SDK and on device in /usr/lib/)
# Note: device has libplat_isp_sdk.so but SDK calls it libakispsdk.so
# Device-only libs (plat_mem, plat_osal, plat_log, plat_dbg) are v330-specific
# and will be resolved at runtime
# Symlink dir to map SDK lib name -> device lib name
FIXUP_DIR = /tmp/libfix

DYN_LIBS = -lplat_vi -lplat_vpss -lplat_common -lplat_thread \
           -lmpi_venc -lmpi_osd -lakv_encode -lplat_isp_sdk

LDFLAGS = -L$(FIXUP_DIR) -L$(PLAT_LIB) -L$(MPI_LIB) -L$(APP_LIB)

TARGET  = rtsp_server

all: $(TARGET)

$(TARGET): rtsp.o
	mkdir -p $(FIXUP_DIR) && ln -sf $(PLAT_LIB)/libakispsdk.so $(FIXUP_DIR)/libplat_isp_sdk.so
	$(CC) -o $@ $< $(LDFLAGS) \
	    -Xlinker "-(" $(STATIC_LIBS) $(DYN_LIBS) -lpthread -lm -ldl -lrt -Xlinker "-)"
	$(STRIP) $@

rtsp.o: rtsp.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f *.o $(TARGET)

.PHONY: all clean
