# ESDM PAV Runtime 

**Note**:

This is a fork of the [Ophidia Server](https://github.com/OphidiaBigData/ophidia-server)

### Description

The Ophidia Server is the Ophidia front-end. It supports WS-I and GSI/VOMS interfaces and works as a web service.
It also provides user authorization and confidentialy (based on TLS/SSL protocol for WS-I).
The module processes JSON Requests structured according to the Ophidia Workflow [JSON Schema](http://ophidia.cmcc.it/documentation/users/appendix/json_request.html) and returns [JSON Responses](http://ophidia.cmcc.it/documentation/users/appendix/json_response.html) back to clients.

### Requirements

In order to compile and run the ESDM PAV Runtime, make sure you have the following packages (all available through CentOS official repositories and the epel repository) properly installed:

1. jansson and jansson-devel
2. libxml2 and libxml2-devel
3. libcurl and libcurl-devel
4. openssl and openssl-devel
5. libssh2 and libssh2-devel
6. gsoap
7. GNU libmatheval (only for selection statement and advanced management of run-time variables)
8. sqlite and sqlite-devel

**Note**:

This product includes software developed by the OpenSSL Project for use in the OpenSSL Toolkit.

### How to Install

If you are building from git, you also need automake, autoconf and libtool. To prepare the code for building run:

```
$ ./bootstrap 
```

The source code has been packaged with GNU Autotools, so to install simply type:

```
$ ./configure --prefix=prefix
$ make
$ make install
```

Type:

```
$ ./configure --help
```

to see all available options.

To run unit tests type:

```
$ make check
```

To generate test coverage report type:

```
$ ./configure --prefix=prefix --enable-code-coverage
$ make check-code-coverage
```

The module needs digital certificates enabling TLS/SSL protected communication. You can create and copy the certificates in *etc/cert* folder under the installation path.  Then, copy *authz* into the installation path and configure your users (you can use the tool *oph\_manage\_user*).

If you want to use the program system-wide, remember to add its installation directory to your PATH.

### How to Launch

```
$ esdm_pav_runtime
```

Type:

```
$ esdm_pav_runtime -h
```

to see all other available options.

