# SDRPP, The SDR software with immediate mode GUI


This project is based on SDR++ code originaly written by Alexandre Rouma. This is modified version of code and explicitly marked as changed, so their problems will not be attributed erroneously to authors of original SDR++ code. 

The project in this repository is maintained by qrp73. The code is provided "as is" under GPL-3.0 license and with no warranty. 


Your feedback and bug reports are welcome. Feel free to report issues and discuss technical details and functionality.

Have good day and 73


## Features

- added SDL2 OpenGL ES backend (enabled by default) which supports Linux KMS DRM mode, it can run without desktop. Tested on Raspberry Pi Zero 2W.
- fixed waterfall zoom
- SNR meter replaced with level meter + level and SNR text indication
   ![20250322_02h03m00s_grim](https://github.com/user-attachments/assets/0eea978c-1f0f-4789-be70-9b47ffc884de)
- unity gain for FFT and window functions
- added Blackman-Harris-7, Blackman-Harris-4, Hamming and Hann window functions
- added 1M FFT
- fixed AM,FM,WFM,SSB,DSB bandwidth
- fixed units misspelling
- improved squelch to reduce false triggering
- rtl_sdr_source: fixed sample conversion
- hpsdr_source: added new module for HPSDR protocol enabled devices
- file_source: code replaced with a new multi-format support (supported: FLOAT 32/64 bit and PCM 8/16/24/32 bit)
- audio_source: added support for mono source, fixed error logging
- recorder: fixed default filename pattern
- recorder: added 24-bit PCM support
- recorder: added FLAC container support (8/16/24/32 bit mono/stereo, see libflac for supported sample rates)
- recorder: added MP3 container support (VBR)
- frequency_manager: fixed jittery label position
- some minor fixes

## SDL2 Wayland mode

Using the Wayland driver allows for smoother graphics with less CPU load. This is especially useful for low-power computers. For example, it provides smoother graphics with reduced CPU usage when running on Raspberry Pi OS Bookworm with the Wayland Labwc or Wayland Wayfire compositor.

By default, SDL2 uses the X11 driver if X11 or XWayland is available on the system. To run the application with the Wayland driver, you need to set the environment variable SDL_VIDEODRIVER=wayland. For example, run the following in the terminal: 

```
SDL_VIDEODRIVER=wayland sdrpp
``` 

This will launch the application using the Wayland driver, which provides about a 20% performance boost compared to the X11 driver.

Additionally, note that SDL2 blocks the Wayland screensaver (idle). To allow the screensaver to function while the application is running, you should set the environment variable SDL_VIDEO_ALLOW_SCREENSAVER=1 for the application.

You can create the following bash script sdrpp-wayland to run the application with Wayland driver:
```
#!/usr/bin/env bash

SDL_VIDEODRIVER=wayland SDL_VIDEO_ALLOW_SCREENSAVER=1 sdrpp

```
and place it into ```/usr/local/bin``` with execute permission.


You can check if the SDL2 backend is being used by running the application in the terminal and looking for the line ```SDL_GetCurrentVideoDriver():``` in the log. This line shows the driver that is actually being used. For example:
```
$ SDL_VIDEODRIVER=wayland sdrpp
[15/03/2025 09:29:32.000] [INFO] SDR++ v1.1.0
[15/03/2025 09:29:32.000] [INFO] Loading config
[15/03/2025 09:29:32.000] [WARN] ConfigManager locked, waiting...
[15/03/2025 09:29:32.000] [INFO] SDL_GetCurrentVideoDriver(): wayland
[15/03/2025 09:29:32.000] [INFO] OpenGL: OpenGL ES 3.1 Mesa 24.2.8-1~bpo12+rpt1
[15/03/2025 09:29:32.000] [INFO] GLSL:   OpenGL ES GLSL ES 3.10
[15/03/2025 09:29:32.000] [INFO] GL_SAMPLES: 0
[15/03/2025 09:29:32.000] [INFO] Loading icons
```


## SDL2 KMSDRM mode

If you are running the application on a system without a graphical desktop (X11, Wayland), SDL2 will automatically select the KMSDRM driver. However, if you are running the application from a virtual terminal with a background-running graphical manager, it may prevent SDL2 from correctly detecting the driver. 

In this case, you can force SDL2 to use the KMSDRM driver by specifying it explicitly with environment variable for application:
```
SDL_VIDEODRIVER=kmsdrm sdrpp
```

You can exit the application in KMSDRM mode using the ```Alt+F4``` key combination.



## Hardware

The only hardware that I have is RTLSDRv3 and custom FPGA DSP chains for different hardware which are using my custom or HPSDR protocol. So, this project is targeted primary on RTLSDRv3 and HPSDR protocol devices. But feel free to report issues with other hardware.


## Source code

You can download the latest code version from this repository: https://github.com/qrp73/SDRPP


## Build & Install

You can install SDRPP with manual build.

### Build and install on Debian

Install pre-requisites:
```
sudo apt update
sudo apt install -y \
   git wget p7zip-full build-essential cmake cmake-curses-gui libtool xxd autoconf \
   libfftw3-dev libglfw3-dev libglew-dev libvolk2-dev libsoapysdr-dev libairspyhf-dev libairspy-dev \
   libiio-dev libad9361-dev librtaudio-dev libhackrf-dev librtlsdr-dev libbladerf-dev liblimesuite-dev \
   libcodec2-dev libzstd-dev portaudio19-dev libsdl2-dev libflac-dev libmp3lame-dev
```
If `libvolk2-dev` is not available, use `libvolk1-dev`.


Build and install:
```
git clone https://github.com/qrp73/SDRPP
cd SDRPP
mkdir -p build && cd build
cmake ..
make -j4
sudo make install
```

Build with SDL2 backend to use in KMS DRM mode (don't requires graphics desktop to run):
```
git clone https://github.com/qrp73/SDRPP
cd SDRPP
mkdir -p build && cd build
cmake -DOPT_BACKEND_GLFW=OFF -DOPT_BACKEND_SDL2=ON ..
make -j4
sudo make install
```

Run:
```
sdrpp
```

When running with SDL2 backend you can exit to command line with Alt+F4.


### Uninstall on Debian

```
cd SDRPP/build
sudo make uninstall
make clean

sudo rm /usr/bin/sdrpp
sudo rm -r /usr/lib/sdrpp
sudo rm -r /usr/share/sdrpp
sudo rm -r /home/<USERNAME>/.config/sdrpp
```


### Build and install on Arch

Install pre-requisites:
```
sudo pacman -Syu
sudo pacman -Sy \
    git wget base-devel cmake core/libtool core/autoconf \
    fftw extra/glfw-x11 extra/glew libvolk extra/soapysdr extra/airspy \
    extra/libiio extra/libad9361 extra/rtaudio extra/hackrf extra/rx_tools extra/bladerf extra/limesuite \
    extra/codec2 core/zstd extra/portaudio rtl-sdr
yay airspyhf-git
```

Build and install:
```
git clone https://github.com/qrp73/SDRPP
cd SDRPP
mkdir -p build
cd build
cmake ..
make -j4
sudo make install
```


## Modules

| Name                 | Stage      | Dependencies      | Option                         | Built by default| Built in Release        | Enabled in SDR++ by default |
|----------------------|------------|-------------------|--------------------------------|:---------------:|:-----------------------:|:---------------------------:|
| airspy_source        | Working    | libairspy         | OPT_BUILD_AIRSPY_SOURCE        | ✅              | ✅                     | ✅                         |
| airspyhf_source      | Working    | libairspyhf       | OPT_BUILD_AIRSPYHF_SOURCE      | ✅              | ✅                     | ✅                         |
| bladerf_source       | Working    | libbladeRF        | OPT_BUILD_BLADERF_SOURCE       | ⛔              | ✅ (not Debian Buster) | ✅                         |
| file_source          | Working    | -                 | OPT_BUILD_FILE_SOURCE          | ✅              | ✅                     | ✅                         |
| hackrf_source        | Working    | libhackrf         | OPT_BUILD_HACKRF_SOURCE        | ✅              | ✅                     | ✅                         |
| hpsdr_source         | Beta       | -                 | OPT_BUILD_HPSDR_SOURCE         | ✅              | ✅                     | ✅                         |
| hermes_source        | Beta       | -                 | OPT_BUILD_HERMES_SOURCE        | ✅              | ✅                     | ✅                         |
| limesdr_source       | Working    | liblimesuite      | OPT_BUILD_LIMESDR_SOURCE       | ⛔              | ✅                     | ✅                         |
| perseus_source       | Beta       | libperseus-sdr    | OPT_BUILD_PERSEUS_SOURCE       | ⛔              | ⛔                     | ⛔                         |
| plutosdr_source      | Working    | libiio, libad9361 | OPT_BUILD_PLUTOSDR_SOURCE      | ✅              | ✅                     | ✅                         |
| rfspace_source       | Working    | -                 | OPT_BUILD_RFSPACE_SOURCE       | ✅              | ✅                     | ✅                         |
| rtl_sdr_source       | Working    | librtlsdr         | OPT_BUILD_RTL_SDR_SOURCE       | ✅              | ✅                     | ✅                         |
| rtl_tcp_source       | Working    | -                 | OPT_BUILD_RTL_TCP_SOURCE       | ✅              | ✅                     | ✅                         |
| sdrplay_source       | Working    | SDRplay API       | OPT_BUILD_SDRPLAY_SOURCE       | ⛔              | ✅                     | ✅                         |
| sdrpp_server_source  | Working    | -                 | OPT_BUILD_SDRPP_SERVER_SOURCE  | ✅              | ✅                     | ✅                         |
| soapy_source         | Working    | soapysdr          | OPT_BUILD_SOAPY_SOURCE         | ✅              | ✅                     | ✅                         |
| spectran_source      | Unfinished | RTSA Suite        | OPT_BUILD_SPECTRAN_SOURCE      | ⛔              | ⛔                     | ⛔                         |
| spectran_http_source | Unfinished | -                 | OPT_BUILD_SPECTRAN_HTTP_SOURCE | ✅              | ✅                     | ⛔                         |
| spyserver_source     | Working    | -                 | OPT_BUILD_SPYSERVER_SOURCE     | ✅              | ✅                     | ✅                         |
| usrp_source          | Beta       | libuhd            | OPT_BUILD_USRP_SOURCE          | ⛔              | ⛔                     | ⛔                         |

## Sinks

| Name               | Stage      | Dependencies | Option                       | Built by default| Built in Release | Enabled in SDR++ by default |
|--------------------|------------|--------------|------------------------------|:---------------:|:----------------:|:---------------------------:|
| android_audio_sink | Working    | -            | OPT_BUILD_ANDROID_AUDIO_SINK | ⛔              | ✅              | ✅ (Android only)          |
| audio_sink         | Working    | rtaudio      | OPT_BUILD_AUDIO_SINK         | ✅              | ✅              | ✅                         |
| network_sink       | Working    | -            | OPT_BUILD_NETWORK_SINK       | ✅              | ✅              | ✅                         |
| new_portaudio_sink | Beta       | portaudio    | OPT_BUILD_NEW_PORTAUDIO_SINK | ⛔              | ✅              | ⛔                         |
| portaudio_sink     | Beta       | portaudio    | OPT_BUILD_PORTAUDIO_SINK     | ⛔              | ✅              | ⛔                         |

## Decoders

| Name                | Stage      | Dependencies | Option                        | Built by default| Built in Release | Enabled in SDR++ by default |
|---------------------|------------|--------------|-------------------------------|:---------------:|:----------------:|:---------------------------:|
| atv_decoder         | Unfinished | -            | OPT_BUILD_ATV_DECODER         | ⛔              | ⛔              | ⛔                         |
| dmr_decoder         | Unfinished | -            | OPT_BUILD_DMR_DECODER         | ⛔              | ⛔              | ⛔                         |
| falcon9_decoder     | Unfinished | ffplay       | OPT_BUILD_FALCON9_DECODER     | ⛔              | ⛔              | ⛔                         |
| kgsstv_decoder      | Unfinished | -            | OPT_BUILD_KGSSTV_DECODER      | ⛔              | ⛔              | ⛔                         |
| m17_decoder         | Beta       | -            | OPT_BUILD_M17_DECODER         | ⛔              | ✅              | ⛔                         |
| meteor_demodulator  | Working    | -            | OPT_BUILD_METEOR_DEMODULATOR  | ✅              | ✅              | ⛔                         |
| radio               | Working    | -            | OPT_BUILD_RADIO               | ✅              | ✅              | ✅                         |
| weather_sat_decoder | Unfinished | -            | OPT_BUILD_WEATHER_SAT_DECODER | ⛔              | ⛔              | ⛔                         |

## Misc

| Name                | Stage      | Dependencies | Option                      | Built by default | Built in Release | Enabled in SDR++ by default |
|---------------------|------------|--------------|-----------------------------|:----------------:|:----------------:|:---------------------------:|
| frequency_manager   | Working    | -            | OPT_BUILD_FREQUENCY_MANAGER | ✅              | ✅               | ✅                         |
| recorder            | Working    | -            | OPT_BUILD_RECORDER          | ✅              | ✅               | ✅                         |
| rigctl_client       | Unfinished | -            | OPT_BUILD_RIGCTL_CLIENT     | ✅              | ✅               | ⛔                         |
| rigctl_server       | Working    | -            | OPT_BUILD_RIGCTL_SERVER     | ✅              | ✅               | ✅                         |
| scanner             | Beta       | -            | OPT_BUILD_SCANNER           | ✅              | ✅               | ⛔                         |
| scheduler           | Unfinished | -            | OPT_BUILD_SCHEDULER         | ⛔              | ⛔               | ⛔                         |


## This software contains code writen by the following contributors

* [qrp73](https://github.com/qrp73)
* [Ryzerth](https://github.com/AlexandreRouma)
* [Aang23](https://github.com/Aang23)
* [Alexsey Shestacov](https://github.com/wingrime)
* [Aosync](https://github.com/aosync)
* [Benjamin Kyd](https://github.com/benkyd)
* [Benjamin Vernoux](https://github.com/bvernoux)
* [Cropinghigh](https://github.com/cropinghigh)
* [Fred F4EED](http://f4eed.wordpress.com/)
* [Howard0su](https://github.com/howard0su)
* John Donkersley
* [Joshua Kimsey](https://github.com/JoshuaKimsey)
* [Manawyrm](https://github.com/Manawyrm)
* [Martin Hauke](https://github.com/mnhauke)
* [Marvin Sinister](https://github.com/marvin-sinister)
* [Maxime Biette](https://github.com/mbiette)
* [Paulo Matias](https://github.com/thotypous)
* [Raov](https://twitter.com/raov_birbtog)
* [Cam K.](https://github.com/Starman0620)
* [Shuyuan Liu](https://github.com/shuyuan-liu)
* [Syne Ardwin (WI9SYN)](https://esaille.me/)
* [Szymon Zakrent](https://github.com/zakrent)
* Youssef Touil
* [Zimm](https://github.com/invader-zimm)


## Libraries used

* [Dear ImGui (ocornut)](https://github.com/ocornut/imgui)
* [json (nlohmann)](https://github.com/nlohmann/json)
* [rtaudio](http://www.portaudio.com/)
* [Portable File Dialogs](https://github.com/samhocevar/portable-file-dialogs)
* [SoapySDR (PothosWare)](https://github.com/pothosware/SoapySDR)

