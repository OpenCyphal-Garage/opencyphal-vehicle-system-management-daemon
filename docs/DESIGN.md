# Open Cyphal Vehicle System Management Daemon for GNU/Linux

This project implements a user-facing C++14 library backed by a GNU/Linux daemon used to asynchronously perform certain common operations on an OpenCyphal network. Being based on LibCyphal, the solution can theoretically support all transport protocols supported by LibCyphal, notably Cyphal/UDP and Cyphal/CAN.

The implementation is planned to proceed in multiple stages. The milestones achieved at every stage are described here along with the overall longer-term vision.

The design of the C++ API is inspired by the [`ravemodemfactory`](https://github.com/aleksander0m/ravemodemfactory) project (see `src/librmf/rmf-operations.h`).

[Yakut](https://github.com/OpenCyphal/yakut) is a distantly related project with the following key differences:

- Yakut is a developer tool, while OCVSMD is a well-packaged component intended for deployment in production systems.

- Yakut is a user-interactive tool with a CLI, while OCVSMD is equipped with a machine-friendly interface -- a C++ API. Eventually, OCVSMD may be equipped with a CLI as well, but it will always come secondary to the well-formalized C++ API.

- OCVSMD will be suitable for embedded Linux systems including such systems running on single-core "cross-over" processors.

- OCVSMD will be robust.

## Long-term vision

Not all of the listed items will be implemented the way they are seen at the time of writing this document, but the current description provides a general direction things are expected to develop in.

OCVSMD is focused on solving problems that are pervasive in intra-vehicular OpenCyphal networks with minimal focus on any application-specific details. This list may eventually include:

- Publish/subscribe on Cyphal subjects with arbitrary DSDL data types loaded at runtime, with the message objects represented as dynamically typed structures. More on this below.
- RPC client for invoking arbitrarily-typed RPC servers with DSDL types loaded at runtime.
- Support for the common Cyphal network services out of the box, configurable via the daemon API:
  - File server running with the specified set of root directories (see Yakut).
  - Firmware update on a directly specified remote node with a specified filename.
  - Automatic firmware update as implemented in Yakut.
  - Centralized (eventually could be distributed for fault tolerance) plug-and-play node-ID allocation server.
- Depending on how the named topics project develops (many an intern has despaired over it), the Cyphal resource name server may also be implemented as part of OCVSMD at some point.
- A possible future node authentication protocol may also be implemented in this project.

Being a daemon designed for unattended operation in embedded vehicular computers, OCVSMD must meet the following requirements:

- Ability to operate from a read-only filesystem.
- Startup time much faster than that of Yakut. This should not be an issue for a native application since most of the Yakut startup time is spent on the Python runtime initialization, compilation, and module importing.
- Local node configuration ((redundant) transport configuration, node-ID, node description, etc) is loaded from a file, which is common for daemons.

### Dynamic DSDL loading

Dynamic DSDL loading is proposed to be implemented by creating serializer objects whose behavior is defined by the DSDL definition ingested at runtime. The serialization method is to accept a byte stream and to produce a DSDL object model providing named field accessors, similar to what one would find in a JSON serialization library; the deserialization method is the inverse of that. Naturally, said model will heavily utilize PMR for storage. An API mockup is given in `dsdl.hpp`.

One approach assumes that instances of `dsdl::Object` are not exchanged between the client and the daemon; instead, only their serialized representations are transferred between the processes; thus, the entire DSDL support machinery exists in the client's process only. This approach involves certain work duplication between clients, and may impair their ability to start up quickly if DSDL parsing needs to be done. Another approach is to use shared-memory-friendly containers, e.g., via specialized PMR.

Irrespective of how the dynamic DSDL loading is implemented, the standard data types located in the `uavcan` namespace will be compiled into both the daemon and the clients, as they are used in the API definition -- more on this below.

### C++ API

The API will consist of several well-segregated C++ interfaces, each dedicated to a particular feature subset. The interface-based design is chosen to simplify testing in client applications. The API is intentionally designed to not hide the structure of the Cyphal protocol itself; that is to say that it is intentionally low-level. Higher-level abstractions can be built on top of it on the client side rather than the daemon side to keep the IPC protocol stable.

The `Error` type used in the API definition here is a placeholder for the actual algebraic type listing all possible error states per API entity.

The main file of the C++ API is the `daemon.hpp`, which contains the abstract factory `Daemon` for the specialized interfaces, as well as the static factory factory (sic) `connect() -> Daemon`.

### Anonymous mode considerations

Normally, the daemon should have a node-ID of its own. It should be possible to run it without one, in the anonymous mode, with limited functionality:

- The Monitor will not be able to query GetInfo.
- The RegisterClient, PnPNodeIDAllocator, FileServer, NodeCommandClient, etc. will not be operational.

### Configuration file format

The daemon configuration is stored in a TSV file, where each row contains a key, followed by at least one whitespace separator, followed by the value. The keys are register names. Example:

```tsv
uavcan.node.id          123
uavcan.node.description This is the OCVSMD
uavcan.udp.iface        192.168.1.33 192.168.2.33
```

For the standard register names, refer to <https://github.com/OpenCyphal/public_regulated_data_types/blob/f9f67906cc0ca5d7c1b429924852f6b28f313cbf/uavcan/register/384.Access.1.0.dsdl#L103-L199>.

### CLI

TBD

### Common use cases

#### Firmware update

Per the design of the OpenCyphal's standard network services, the firmware update process is entirely driven by the node being updated (updatee) rather than the node providing the new firmware file (updater). While it is possible to indirectly infer the progress of the update process by observing the offset of the file reads done by the updatee, this solution is fragile because there is ultimately no guarantee that the updatee will read the file sequentially, or even read it in its entirety. Per the OpenCyphal design, the only relevant parameters of a remote node that can be identified robustly are:

- Whether a firmware update is currently in progress or not.
- The version numbers, CRC, and VCS ID of the firmware that is currently being executed.

The proposed API allows one to commence an update process and wait for its completion as follows:

1. Identify the node that requires a firmware update, and locate a suitable firmware image file on the local machine.
2. `daemon.get_file_server().add_root(firmware_path)`, where `firmware_path` is the path to the new image.
3. `daemon.get_node_command_client().begin_software_update(node_id, firmware_name)`, where `firmware_name` is the last component of the `firmware_path`.
4. Using `daemon.get_monitor().snapshot()`, ensure that the node in question has entered the firmware update mode. Abort if not.
5. Using `daemon.get_monitor().snapshot()`, wait until the node has left the firmware update mode.
6. Using `daemon.get_monitor().snapshot()`, ensure that the firmware version numbers match those of the new image.

It is possible to build a convenience method that manages the above steps. Said method will be executed on the client side as opposed the daemon side.

##### Progress monitoring

To enable monitoring the progress of a firmware update process, the following solutions have been considered and rejected:

- Add an additional general-purpose numerical field to `uavcan.node.ExecuteCommand.1` that returns the progress information when an appropriate command (a new standard command) is sent. This is rejected because an RPC-based solution is undesirable.

- Report the progress via `uavcan.node.Heartbeat.1.vendor_specific_status_code`. This is rejected because the VSSC is vendor-specific, so it shouldn't be relied on by the standard.

The plan that is tentatively agreed upon is to use an existing primitive message for progress reporting. The `uavcan.node.ExecuteCommand.1.4` RPC will optionally accept the subject-ID to publish progress messages at.

## Milestone 0

This milestone includes the very barebones implementation, including only:

- The daemon itself, compatible with System V architecture only. Support for systemd will be introduced in a future milestone.
- Running a local Cyphal/UDP node. No support for other transports yet.
- Loading the configuration from the configuration file as defined above.
- File server.
- Node command client.

These items will be sufficient to perform firmware updates on remote nodes, but not to monitor the update progress. Progress monitoring will require the Monitor module.
