### Using Teledyne Dalsa SDK

#### Pre-requisites:

*Boost v1.55*
```shell
wget -O boost_1_55_0.tar.gz http://sourceforge.net/projects/boost/files/boost/1.55.0/boost_1_55_0.tar.gz/download
tar xzvf boost_1_55_0.tar.gz
cd boost_1_55_0/
./bootstrap.sh --prefix=/usr
./b2 stage threading=multi link=shared
n=`cat /proc/cpuinfo | grep "cpu cores" | uniq | awk '{print $NF}'`
./b2 --with=all -j $n install
```

*Packages:*
- g++
- libgtk-3-dev
- make
- libevent-dev
- libglade2-0
- libglade2-dev
- libx11-dev
- libxext-dev

*Dependencies(Automatically built)*
- [AMQP-CPP v2.6.2](https://github.com/CopernicaMarketingSoftware/AMQP-CPP/archive/v2.6.2.tar.gz)

*Environment variables:*
- GENICAM_ROOT_V3_0=/opt/genicam_v3_0
- GENICAM_CACHE_V3_0=/var/opt/genicam/xml/cache
- GENICAM_LOG_CONFIG_V3_0=/opt/genicam_v3_0/log/config-unix
- GIGEV_XML_DOWNLOAD=/usr/DALSA/GigeV

*Installing SDK:*
> -- After downloading the Linux SDK --

```bash
cp GIGE-V-Framework_<arch>_<ver>.tar.gz $HOME
cd $HOME
tar -zxf GIGE-V-Framework_<arch>_<ver>.tar.gz
cd DALSA
sudo -i
./corinstall
```

> The SDK has no dev support for linux versions.
> The (semi-)helpful documentation can be found in GigE-V_Framework_Programmers_Manual.pdf

*Git*
- Clone [https://github.com/SergiusTheBest/plog](https://github.com/SergiusTheBest/plog)
- move the contents of `include` inside the project folder (check Directory Structure for more information).

#### Directory Structure:

The `common` directory needs to be copied with the source code directory.

The following hierarchy should be maintained (according to current CMakefile)

```

- capture_code
  | - cpp file
  | - archdefs.mk
  | - corenv.h
  | - CMakeLists.txt
  [] - Makefile*
  | - cmake
    | - Modules
      | - ...
- common
  | - GevUtils.c
  | - SapExUtil.c
  | - SapExUtil.h
  | - SapX11Util.h
  | - X_Display_utils.c
  | - X_Display_utils.h
- plog
  | - <...>.h
  | - ...
```
> *Only there for historical reference. This Makefile won't build the whole setup. It cannot satisfy all dependencies

Check the `CMakeLists.txt` for more information
