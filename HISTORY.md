
## v1.6.0 - 2023-01-22

### Fixed:

- Bugs in unit tests
- Minor bugs

### Added:

- Options 'src_path' and 'input' to operator OPH_FOR
- Support for CentOS9
- Operator OPH_LOGGINGBK
- Support for SLURM and LSF to task scheduler

### Changed:

- Use task name in job name at resource manager level
- Requirements for librabbitmq
- Scripts to start, stop and check tasks
- Operator OPH_PAV_WORKER to get information and to monitor the workers

## v1.4.0 - 2021-12-22

### Fixed:

- Bug in parsing for comments in JSON Requests

### Added:

- Possibility to create one branch for each file of a folder with parallel OPH_FOR [#63](https://github.com/OphidiaBigData/ophidia-server/pull/63)
- Submission control to limit the number of running tasks or busy cores [#62](https://github.com/OphidiaBigData/ophidia-server/pull/62)
- Simple scripts to generate digital certificates
- Workflow variable indicating the number of loops of a 'OPH_FOR' statement

### Changed:

- Stronger integration with the Ophidia Analytics Framework
- Improved support for more complex CDO commands
- Improved overall multi-node execution management
- Update runtime to support new JSON schema definition (e.g., checkpointing feature, error recovery, mandatory/optional parameters)
- Improved configure options to semplify the installation process

## v1.2.0 - 2021-06-25

### Added:

- Support for multi-node execution via RabbitMQ-based scheduling system
- Initial support for Singularity-based containerized version
- New experiment task type 'control' and support for related operators

### Changed:

- Configuration files, scripts and Makefiles to simplify the building procedure
- Binary file names and default paths
- Updated execution status names to be compliant with the new JSON schema

### Fixed:

- Reference to non-mandatory and obsolete dependencies 
- Bug related to task history management
- Bug in saving extended experiment request

## v1.0.0 - 2020-10-01

- Initial package release. The version includes:

  - PAV documents (based on the ESDM PAV schema) parser and validator 
  - The runtime engine for the execution of PAV workflows (DAGs of tasks)
  - Support for Ophidia, CDO and generic tools task types
  - SQLite-based internal catalog for task status management

NOTE: This repo has been forked from the ophidia-server. 


