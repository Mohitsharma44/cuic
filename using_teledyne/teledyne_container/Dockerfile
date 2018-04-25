FROM        ubuntu:16.04 AS base

WORKDIR     /tmp/workdir

RUN     apt-get -y update && \
        apt-get install -y --no-install-recommends \
		software-properties-common \
		ca-certificates \
		git \
		g++ \
		gcc \
		libgtk-3-dev \
		make \
		libevent-dev \
		libglade2-0 \
		libglade2-dev \
		libx11-dev \
		libxext-dev \
		cmake \
		libpcap0.8 \
		libcap2 \
		ethtool  \
		libboost-all-dev \
		libevent-dev && \
        apt-get autoremove -y && \
        apt-get clean -y

RUN	add-apt-repository ppa:wireshark-dev/stable && \
	apt-get -y update && \
	DEBIAN_FRONTEND=noninteractive apt-get -y install wireshark && \
	echo "wireshark-common wireshark-common/install-setuid boolean false" | debconf-set-selections && \
	DEBIAN_FRONTEND=noninteractive dpkg-reconfigure wireshark-common

COPY	DALSA	/tmp/workdir/DALSA

RUN	cd /tmp/workdir/DALSA && \
	./corinstall install

RUN	nproc=`cat /proc/cpuinfo | grep "cpu cores" | uniq | awk '{print $NF}'` && \
	git clone https://mohitsharma44@github.com/Mohitsharma44/cuic /tmp/workdir/cuic && \
	cd /tmp/workdir/cuic/using_teledyne/ && mkdir build && cd build && \
	 cmake -D BOOST_ROOT=/usr/include -DBoost_LIBRARY_DIRS:FILEPATH=/usr/lib/x86_64-linux-gnu .. && \
	make -j${nproc} 2>&1 || echo "Shhh.... Makefile crapped out, I knew this would happen due to crappy Makefile dependencies. I'll try again" && \
	sleep 5 && \
	make clean && \
	make -j${nproc}

#ENTRYPOINT	["/tmp/workdir/cuic/using_teledyne/build/capture_demo/genicam_cpp_demo", "192.168.1.7", "1mtcNorth"]

