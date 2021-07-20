
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

