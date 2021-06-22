# ESDM PAV Runtime Container

### Description

This readme document explains how to build the ESDM PAV Runtime container image and start the software from the container.

### Requirements

In order to build and run the ESDM PAV Runtime container image, make sure you have the following packages (mostly available through CentOS official repositories and the epel repository) properly installed:

1. Docker
2. Singularity
3. MPI (mpirun command is required)
4. Ophidia Analytics Framework

### How to build the container image

To build the Singularity image from the repository, within this folder, you can run the following commands:

```
$ sudo docker build -t esdm-pav-runtime . 

$ sudo singularity build esdm-pav-runtime.sif docker-daemon://esdm-pav-runtime:latest
```

### How to start the runtime from the container

Before starting the containers create the local folders for the ssh public key (it will be used later to allow the container to run the commands on the host) and the log:

```
$ export MYBINDS=<PATH TO LOCAL BINDS ON HOST>

$ mkdir -p $MYBINDS/{connect,log}
```

Then you can start the Singularity container with (rember to replace <PATH TO OPHIDIA FRAMEWORK> with the actual one):

```
$ singularity run --writable-tmpfs -B $MYBINDS/connect/:/connect/ -B $MYBINDS/log/:/usr/local/esdmpav/esdm-pav-runtime/log/ esdm-pav-runtime.sif $(hostname -I | awk '{print $1}') <PATH TO OPHIDIA FRAMEWORK> 0 $USER
```

Note that the third argument can be set to 1 to start the runtime in debug mode.

Finally, add the container public key in those authorized on the hosts to allow submission of tasks from the container on the host machine.

```
$ cat $MYBINDS/connect/id_rsa.pub >> $HOME/.ssh/authorized_keys
```
