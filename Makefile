TARGET = Another_world
TITLE_ID = SOMO00002
PSVITAIP = 192.168.0.193

SRCS = aifcplayer.cpp bitmap.cpp file.cpp fs.cpp engine.cpp  graphics_soft.cpp \
	script.cpp mixer.cpp pak.cpp resource.cpp resource_nth.cpp resource_win31.cpp \
	resource_3do.cpp systemstub_sdl.cpp sfxplayer.cpp staticres.cpp unpack.cpp \
	util.cpp video.cpp main.cpp
	
OBJS = $(SRCS:.cpp=.o)

INCDIR =-I/include -I/local/vitasdk/$(PREFIX)/include/vita2d  -I/local/vitasdk/$(PREFIX)/include/SDL2 -I/local/vitasdk/$(PREFIX)/include/SDL2_mixer

SDL_CFLAGS = `sdl2-config --cflags`
SDL_LIBS = `sdl2-config --libs` -
OLD=lSDL2_mixer -lGL

DEFINES = -DBYPASS_PROTECTION

CXXFLAGS := -g -O -MMD -Wall $(SDL_CFLAGS) $(DEFINES)

LIBS +=  -lSDL2  -lSDL2_mixer -lvita2d  -ldebugnet -lSceNetCtl_stub -lSceNet_stub \
	     -lSceKernel_stub -lSceGxm_stub -lSceDisplay_stub -lSceCtrl_stub -lSceAudio_stub \
		 -lSceSysmodule_stub -lScePgf_stub -lSceCommonDialog_stub \
		 -lScePower_stub -lfreetype -lpng -ljpeg -lz -lm -lc

PREFIX   = arm-vita-eabi
CC       = $(PREFIX)-gcc
CXX      = $(PREFIX)-g++
CFLAGS   =  $(INCDIR) -fpermissive  -Wl,-q -Wall -O3  -Wno-unused-variable -Wno-unused-function  -Wno-unused-but-set-variable -DPSVITA
CXXFLAGS = $(CFLAGS)  -std=c++11
ASFLAGS  = $(CFLAGS)
		 

all: $(TARGET).vpk

%.vpk: eboot.bin
	vita-mksfoex  -s TITLE_ID=$(TITLE_ID) "$(TARGET)" param.sfo
	vita-pack-vpk -s param.sfo -b eboot.bin \
		--add pkg/sce_sys/icon0.png=sce_sys/icon0.png \
		--add pkg/sce_sys/livearea/contents/bg.png=sce_sys/livearea/contents/bg.png \
		--add pkg/sce_sys/livearea/contents/startup.png=sce_sys/livearea/contents/startup.png \
		--add pkg/sce_sys/livearea/contents/template.xml=sce_sys/livearea/contents/template.xml \
	$(TARGET).vpk
	
eboot.bin: $(TARGET).velf
	vita-make-fself -s $< $@
	
%.velf: %.elf	
	vita-elf-create $< $@

$(TARGET).elf: $(OBJS)
	$(CXX) $(CXXFLAGS) $^ $(LIBS) -o $@

%.o: %.png
	$(PREFIX)-ld -r -b binary -o $@ $^
%.o: %.txt
	$(PREFIX)-ld -r -b binary -o $@ $^

%.o : %.cpp
	$(CXX) -c $(CXXFLAGS) -o $@ $<
	
	
vpksend: $(TARGET).vpk
	curl -T $(TARGET).vpk ftp://$(PSVITAIP):1337/ux0:/
	@echo "Sent."
send: eboot.bin
	curl -T eboot.bin ftp://$(PSVITAIP):1337/ux0:/app/$(TITLE_ID)/
	@echo "Sent."
clean:
    
	@rm -rf $(TARGET).velf $(TARGET).elf $(TARGET).vpk eboot.bin param.sfo *.o