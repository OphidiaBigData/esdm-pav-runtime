FROM centos:7 AS builder

#TODO CHANGE BRANCH
ARG branch_esdmpavruntime=feature-multinode
ARG framework_path=/usr/local/ophidia/oph-cluster/oph-analytics-framework

USER root

EXPOSE 11732

RUN cd / && \
	yum -y install initscripts sudo && \
	sudo yum -y update yum && \
	sudo yum -y update && \
	sudo yum -y install epel-release && \
    sudo yum -y install autoconf automake gcc gcc-c++ git make && \
	sudo yum -y install libtool libtool-ltdl\* && \
	sudo yum -y install compat-guile18 compat-guile18-devel flex flex-devel guile-devel guile bison && \
	sudo yum -y install sed curl wget bzip2 perl && \
	yum -y install openssl openssl-devel libcurl-devel libxml2\* jansson\* libssh2\* && \
	yum -y install sshpass && \
	sudo yum -y install libssh2-devel openssl glib\* libsmbios\* zlib\* && \
	yum install -y libsqlite3x-devel && \
	yum -y clean all

RUN mkdir -p /repo && \
	mkdir -p /usr/local/esdmpav && \
	mkdir -p /usr/local/esdmpav/extra && \
	sudo useradd esdmpav && \
	usermod -u 1000 esdmpav && \
	groupmod -g 1000 esdmpav && \
	sudo chown -R esdmpav:esdmpav /usr/local/esdmpav

#LIBMATHEVAL
RUN cd /repo && \
	curl -O http://ftp.gnu.org/gnu/libmatheval/libmatheval-1.1.11.tar.gz && \
	tar -xzf libmatheval-1.1.11.tar.gz && \
	cd /repo/libmatheval-1.1.11 && \
	./configure --prefix=/usr/local/esdmpav/extra && \
	make -s > /dev/null && \
	sudo make install -s > /dev/null

#BUILD ESDM-PAV-RUNTIME
RUN cd /repo && \
	sudo yum -y clean all && \
	mkdir -p /usr/local/esdmpav/src && \
	cd /usr/local/esdmpav/src && \
	git clone https://github.com/OphidiaBigData/esdm-pav-runtime && \
	cd /usr/local/esdmpav/src/esdm-pav-runtime && \
	git checkout $branch_esdmpavruntime && \
    cd /usr/local/esdmpav/src/esdm-pav-runtime && \
	./bootstrap && \
	./configure --prefix=/usr/local/esdmpav/esdm-pav-runtime --with-framework-path=$framework_path --with-matheval-path=/usr/local/esdmpav/extra/lib && \
	make -s > /dev/null && \
	make install -s > /dev/null && \
	mkdir -p /usr/local/esdmpav/esdm-pav-runtime/log

#certificates generation
RUN openssl req -newkey rsa:1024 \
	    -passout pass:abcd \
	    -subj "/" -sha1 \
	    -keyout rootkey.pem \
	    -out rootreq.pem && \
	openssl x509 -req -in rootreq.pem \
	    -passin pass:abcd \
	    -sha1 -extensions v3_ca \
	    -signkey rootkey.pem \
	    -out rootcert.pem &&\
	cat rootcert.pem rootkey.pem  > cacert.pem && \

	openssl req -newkey rsa:1024 \
	    -passout pass:abcd \
	    -subj "/" -sha1 \
	    -keyout serverkey.pem \
	    -out serverreq.pem && \
	openssl x509 -req \
	    -in serverreq.pem \
	    -passin pass:abcd \
	    -sha1 -extensions usr_cert \
	    -CA cacert.pem  \
	    -CAkey cacert.pem \
	    -CAcreateserial \
	    -out servercert.pem && \
	cat servercert.pem serverkey.pem rootcert.pem > myserver.pem

#certificates installation
RUN mkdir -p /usr/local/esdmpav/esdm-pav-runtime/etc/cert && \
	cp cacert.pem myserver.pem  /usr/local/esdmpav/esdm-pav-runtime/etc/cert

#create log file
RUN touch /usr/local/esdmpav/esdm-pav-runtime/log/esdm-pav-runtime.log 

# change some permissions
RUN chmod -R 775 /home/esdmpav && \
	rm -rf /repo

RUN sudo yum -y install openssh-server && \
	echo "Port 2222" >> /etc/ssh/sshd_config
RUN su -c "mkdir -p /home/esdmpav/.ssh" esdmpav && \
	su -c "touch /home/esdmpav/.ssh/authorized_keys" esdmpav && \
    su -c "ssh-keygen -t rsa -f /home/esdmpav/.ssh/id_rsa -P \"\"" esdmpav && \
    mkdir -p /connect/

RUN sudo chmod -R 775 /usr/local/esdmpav && \
	sudo chown esdmpav:esdmpav /usr/local/esdmpav/ && \
	sudo chmod -R 775 /usr/local/ophidia/html && \
	sudo chown -R esdmpav:esdmpav /usr/local/ophidia/html

#configure SQLite DB for esdm-pav-runtime
RUN cd /usr/local/esdmpav/esdm-pav-runtime/etc/ && \
	cat esdm-pav-runtime-db.sql | sqlite3 esdm-pav-runtime-db.db && \
	sudo chmod 775 /usr/local/esdmpav/esdm-pav-runtime/etc/esdm-pav-runtime-db.db && \
	sudo chown esdmpav:esdmpav /usr/local/esdmpav/esdm-pav-runtime/etc/esdm-pav-runtime-db.db

RUN sed -Ei "s|RMANAGER_CONF_FILE=/usr/local/esdmpav/esdm-pav-runtime/etc/rmanager.conf|RMANAGER_CONF_FILE=/usr/local/esdmpav/esdm-pav-runtime/etc/rms/local_container.conf|g" /usr/local/esdmpav/esdm-pav-runtime/etc/server.conf

COPY entrypoint.sh /
ENTRYPOINT ["/entrypoint.sh"]
#######################################################################################
FROM centos:7

USER root

EXPOSE 11732

RUN cd / && \
	#COMMON_SW
	yum -y install initscripts sudo && \
	sudo yum -y update yum && \
	sudo yum -y update && \
	sudo yum -y install epel-release && \
	sudo yum -y install sed curl wget && \
	yum -y install openssl sshpass openssh-server openssh-clients && \
	#FOR LIBMATHEVAL
	sudo yum -y install compat-guile18 compat-guile18-devel flex-devel guile-devel guile bison && \
	#FOR ESDM-PAV-RUNTIME
	sudo yum -y install jansson libcurl libssh2 libxml2 && \		
	yum -y clean all

RUN mkdir -p /usr/local/esdmpav/extra && \
	sudo useradd esdmpav && \
	usermod -u 1000 esdmpav && \
	groupmod -g 1000 esdmpav && \
	sudo chown -R esdmpav:esdmpav /usr/local/esdmpav

#LIBMATHEVAL
COPY --from=builder /usr/local/esdmpav/extra /usr/local/esdmpav/extra

#ESDM-PAV-RUNTIME & CERTIFICATES
RUN sudo yum -y clean all && \
	chmod -R 775 /home/esdmpav && \
	mkdir -p /connect/
COPY --from=builder /usr/local/esdmpav/esdm-pav-runtime /usr/local/esdmpav/esdm-pav-runtime
COPY --from=builder /usr/local/esdmpav/extra /usr/local/esdmpav/extra
COPY --from=builder /usr/local/ophidia/html /usr/local/ophidia/html
RUN sudo chmod -R 775 /usr/local/ophidia/html && \
	sudo chown -R esdmpav:esdmpav /usr/local/ophidia/html
RUN sudo chmod -R 775 /usr/local/esdmpav/ && \
	sudo chown -R esdmpav:esdmpav /usr/local/esdmpav/

#SSH_TUNNEL
COPY --from=builder /etc/ssh/sshd_config /etc/ssh/sshd_config
COPY --from=builder /home/esdmpav/.ssh/ /home/esdmpav/.ssh/

RUN sudo chown -R esdmpav:esdmpav /home/esdmpav/.ssh/

USER esdmpav

COPY entrypoint.sh /
ENTRYPOINT ["/entrypoint.sh"]
