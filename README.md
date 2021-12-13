# ESDM-PAV Runtime 

**Note**:

This is a fork of the [Ophidia Server](https://github.com/OphidiaBigData/ophidia-server)

### Description

The ESDM-PAV runtime support the execution of data post-processing, analytics and visualisation (PAV) experiments in the field of climate and weather applications.
It integrates the [Earth System Data Middleware (ESDM)](https://github.com/ESiWACE/esdm) for improved parallel data access for PAV use cases. The runtime can support complex experiments composed of multiple analytics tasks in the form of DAGs.

A Python client is provided to build and submit the experiments to the runtime (https://github.com/OphidiaBigData/esdm-pav-client).

### Requirements

In order to compile and run the ESDM-PAV Runtime, make sure you have the following packages properly installed:

1. jansson and jansson-devel
2. libxml2 and libxml2-devel
3. libcurl and libcurl-devel
4. openssl and openssl-devel
5. libssh2 and libssh2-devel
6. gsoap
7. GNU libmatheval (only for selection statement and advanced management of run-time variables)
8. sqlite and sqlite-devel
9. RabbitMQ server (only for multi-node configuration)
10. RabbitMQ C client library (only for multi-node configuration)
11. ESDM library (only for multi-node configuration)
12. CDO
13. Ophidia framework

**Note**:

Most of the libraries and tools can be installed from the Linux operating system package managers, such as yum (CentOS) or apt-get (Debian). Other modules can be installed or built following the instructions provided on the official documentation pages:
* GNU Matheval can be built from source http://ftp.gnu.org/gnu/libmatheval/;
* RabbitMQ server must be set up and configured https://www.rabbitmq.com/download.html; 
* The RabbitMQ C client library can be installed from the related GitHub repository: https://github.com/alanxz/rabbitmq-c;
* Ophidia can be installed following (Ophidia I/O server, Ophidia Analytics Framework and OphidiaDB): http://ophidia.cmcc.it/documentation/admin/index.html;
* Instructions for the ESDM library are available at: https://github.com/ESiWACE/esdm;
* Documentation for the setup of CDO: https://code.mpimet.mpg.de/projects/cdo/wiki#Download-Compile-Install.

This product includes software developed by the OpenSSL Project for use in the OpenSSL Toolkit.

### How to Install

If you are building from git, you also need automake, autoconf and libtool. To prepare the code for building run:

```
$ git clone --recurse-submodules https://github.com/OphidiaBigData/esdm-pav-runtime.git
```

```
$ ./bootstrap 
```


**Multi node setup:**

The source code has been packaged with GNU Autotools, so to install simply type:

```
$ ./configure --prefix=prefix --with-librabbitmq-header-path=/path/to/rabbitmqclib/include --with-librabbitmq-lib-path=/path/to/rabbitmqclib/lib --with-matheval-path=/path/to/libmatheval --with-netcdf-path=/path/to/netcdf --with-esdm-path=/path/to/esdm --with-ophidiaio-server-path=/path/to/ophidia-io-server
$ make
$ make install
```

Type:

```
$ ./configure --help
```

to see all available options.

Once the code is built and installed the following script can be executed to finalise the configuration:
```
$ $prefix/sbin/esdm_pav_config.sh -r path/to/rabbitmqserver
```

Note that it is possible to use the configuration script to set other system configuration parameters passing:
* -u <RabbitMQ username (default=esdmpav)>
* -p <RabbitMQ password (default=esdmpav)>
* -k <Worker killer (default=kill)>: command used to kill the worker processes
* -n <Worker thread number (default=10)>: maximum number of thread managed by each worker
* -w <Max worker number (default=10)>
* -c <CDO bin directory (default=)>]


**Single node setup:**

In case of single note setup use this configure line:

```
$ ./configure --prefix=prefix --with-matheval-path=/path/to/libmatheval
$ make
$ make install
```


Once the code is built and installed the following script can be executed to finalise the configuration:
```
$ $prefix/sbin/esdm_pav_config.sh
```


If you want to use the program system-wide, remember to add its installation directory to your `$PATH`.


### How to Launch

In the single node setup the runtime can be started as follows:
```
$ prefix/bin/esdm_pav_runtime &
```

In case of multi-node setup, all the following processes need instead to be started:
```
$ path/to/rabbitmqserver/sbin/rabbitmq-server -detached
$ $prefix/bin/esdm_pav_runtime &
$ $prefix/bin/esdm_pav_runtime_dbmanager &
$ $prefix/bin/esdm_pav_runtime_worker &
```

The last command can be executed on each node used for the computation. 

To facilitate the properly launch of the various executables, the following script can be run:

```
$ $prefix/sbin/run_esdm_pav.sh
```

It is also possible to start either the server or worker executables only by passing “master” or “worker” options respectively to the previous command.


Acknowledgement
---------------

This software has been developed starting from the [Ophidia Server](https://github.com/OphidiaBigData/ophidia-server) in the context of the *[ESiWACE2](http://www.esiwace.eu)* project: the *Centre of Excellence in Simulation of Weather and Climate in Europe phase 2*. ESiWACE2 has received funding from the European Union’s Horizon 2020 research and innovation programme under grant agreement No. 823988.
