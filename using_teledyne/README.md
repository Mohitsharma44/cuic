### Using Teledyne Dalsa SDK

#### Pre-requisites:

*Packages:*
- g++
- libgtk-3-dev
- make
- libglade2-0
- libglade2-dev
- libx11-dev
- libxext-dev

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

#### Directory Structure:

The `common` directory needs to be copied with the source code directory.

The following hierarchy should be maintained (according to current Makefile)

```

- capture_code
  | - cpp file
  | - archdefs.mk
  | - corenv.h
  | - Makefile
- common
  | - GevUtils.c
  | - SapExUtil.c
  | - SapExUtil.h
  | - SapX11Util.h
  | - X_Display_utils.c
  | - X_Display_utils.h
  
```

Check the `Makefile` for more information
