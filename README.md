# *omphalos* by nick black (<nickblack@linux.com>)
--------------------------------------------------

[![Build Status](https://drone.dsscaw.com:4443/api/badges/dankamongmen/omphalos/status.svg)](https://drone.dsscaw.com:4443/dankamongmen/omphalos)

> Gaze into your *omphalos*. — James Joyce, *Ulysses*

Omphalos is an integrated tool for network enumeration and domination. It
is designed around Linux's rtnetlink(7) layer and PACKET_MMAP capabilities.
Whereas other tools are geared towards either reconnaissance or directed
attacks, omphalos is designed to "spray the area".

- [Requirements](#Requirements)
- [Building and installation](#Building-and-installation)
- [Usage](#usage)
- [Hacking](#hacking)
  * [Networking](#networking)
  * [UIs/Clients](#uis-clients)
  * [Porting](#porting)
- [FAQs](#faqs)
- [Thanks](#thanks)

-------------------------------------------------------------------
## Requirements

CMake and a C compiler are used at buildtime.

libnl3, libpcap, libz, libiw, and libcap are used at both build and runtime.

Omphalos currently only builds or runs on a Linux kernel with `PACKET_MMAP`
sockets. Packet transmission requires at least a 2.6.29 kernel.

The line-based UI (`omphalos-readline`) requires GNU Readline.

The fullscreen UI (`omphalos`) requires Notcurses 2.0.1+.

arp-scan's `get-oui` program is used to build the IANA OUI file.

GnuPG is used to verify signed tarballs.

Manual page and XHTML documentation require Pandoc.

Your TERM and LOCALE/LANG environment variables need be correctly set to make
full use of `omphalos`. Ensure that a UTF-8 locale is being used, and that your
terminal definition supports 256 colors or RGB.

-------------------------------------------------------------------
## Building and installation

* mkdir build
* cd build
* cmake ..
* make
* make test (requires `-DUSE_SUDO=1` or `sudo`)
* make livetest (requires `-DUSE_SUDO=1` or `sudo`)
* sudo make install

Supported build options include:

`USE_NOTCURSES`: Use Notcurses to build a fullscreen version
`USE_PANDOC`: Use Pandoc to build the manual pages
`USE_READLINE`: Use libreadline to build a line-based version
`USE_SUDO`: Use sudo to bless the binaries with necessary capabilities

If filesystem-based capabilities are supported, it might be desirable to bestow
`CAP_SETUID` privileges (this is *not* equivalent to a setuid binary). See
"Usage" regarding switch to the "nobody" user when `CAP_SETUID` is possessed:
this can help defend users from malicious files, even if they're not allowed to
open packet sockets. If non-root users should be able to use network
capabilities, add CAP_NET_ADMIN and CAP_NET_RAW to the binary. This can be
accomplished by building with `-DUSE_SUDO=1` or running `tools/addcaps`.

If `MADV_HUGEPAGE` is available at compilation time, `madvise()` will be used to
advise hugepage backing for important, large data structures including the
ringbuffers. This could improve performance.

-------------------------------------------------------------------
## Usage

Several capabilities are required for omphalos's usage of packet and
netlink sockets. Omphalos *does not* need to run as root, and generally
should not be. See "Building and installation" for details on setting up
the omphalos binary's privileges.

If omphalos possesses the CAP_SETUID capability (as it always will if run
as root), it will by default attempt to become the "nobody" user. This can
be suppressed via an empty argument to the -u option (-u ""), but helps
defend against malicious input.

Some fonts do not support the full range of Unicode glyphs used by omphalos, or
render them in an unexpected way. Fonts known to cause problems include:

 - Terminus (vertical lines don't match up with rounded corners)
 - Computer Modern (lmodern) (numerous problems)
 - Cortoba (numerous problems)
 - Courier (spacing issues)
 - Electron (numerous problems)
 - FreeMono (spacing issues)
 - Nimbus (spacing issues)

Fonts known to work include:

 - Arial
 - Bitstream Vera Sans
 - Comfortaa
 - Consolas
 - DejaVu Sans Mono
 - Droid Sans Mono
 - Inconsolata
 - Junicode
 - Liberation
 - PragmataPro
 - Sansation

-------------------------------------------------------------------
## Hacking

***************************************
### Networking

We cannot assume that we will see all control traffic, due to switched
networks, wireless topography, physical/network-layer encryption and drops.
Packets can be dropped (possibly silently) at the card, in the networking
stack (thus being neither copied nor processed), or due to a full
ringbuffer (thus being processed but not copied). It thus cannot be assumed
that all packets processed by the machine are able to be read, nor that all
packets seen on the wire even generate a statistic.

We cannot transmit with another station's hardware address; this will not
be generally allowed in a switched network.

We cannot assume that we have an IP address on a local network; this
requires configuration, and will never be the case for eg a bridge.

We cannot assume other hosts are not misconfigured, broken, or adversarial.

***************************************
### UIs/clients

Callback functions might be called by any number of different threads,
possibly concurrently. No relationship can be assumed between threads and
callbacks.

Callbacks are like interrupt handlers. There is a finite ringbuffer for
incoming packets. Packet handling must proceed at wire rate, or else the
ringbuffer will eventually be filled up. GUI events cannot generally
proceed at wire rate, so packet handlers must generally aggregate events
and dispatch work.

You can associate opaque state with an object passed to a callback by
returning a pointer to that opaque state. Returning NULL will disassociate
any attached opaque state. Don't keep a private store of things like
interface data or network sessions; use the general accessors.

It's fine to create threads in your UI, but do not do it until after
calling omphalos_setup(), since this drops permissions and sets up signal
masking necessary for cancellation to function properly. If there are
signals your UI threads must handle, mask them prior to calling
omphalos_setup(), unmask them in the handling thread, and remask them prior
to calling omphalos_init(). Masking them prior to calling omphalos_setup()
is not currently necessary, but will be should it ever spawn threads.

A diagnostic callback must be defined. omphalos_setup() will provide a
default fprintf(stderr,...) callback; you can change it prior to calling
omphalos_init(), but omphalos_init() will error out if it is set to NULL.
There is not yet any means to manage diagnostic output in omphalos_setup().

A packet callback will only reference an incoming interface for which the
device event callback has been successfully invoked, without an intervening
device removal callback invocation. A device removal callback can be invoked
with no corresponding device event callback. Host callbacks follow the same
rules as packet callbacks with regard to device callbacks.

***************************************
### Porting

Omphalos uses several advanced features of the Linux kernel directly, and
many use cases require other functionality. Porting will involve, at
minimum:

 - a means of reading packets from the wire (traditional packet(7) sockets,
    libpcap, etc)
 - a means of transmitting packets on the wire (traditional packet(7)
    sockets)
 - a means of acquiring link, address, route and neighbor information from
    the kernel (we use rtnetlink). this has traditionally been a messy set
    of ioctl()s on linux, which would have to be poll()ed.
 - a means of acquiring physical information from the kernel, especially
    for wireless devices (we use nl80211).
 - a means of dropping general privileges, but retaining those capabilities
    necessary for network activity (perhaps a client/server process pair
    communicating over PF_UNIX sockets).

---------------------------------------
## FAQs

> I see hosts with 0 packets sent or transmitted.

Most likely they were present in your system's ARP cache.

> I see a lot of "protocol 0800 is buggy, dev XXX" messages in dmesg.

This is a diagnostic issued by the kernel's packet socket mechanism. It
will show up if you run tcpdump, wireshark, or any other user of PF_PACKET
sockets on the interface. It has nothing to do with omphalos or the packets
it generates.

---------------------------------------
## Thanks

* Jeremy Stretch of PacketLife.net, for supplying PCAP-format unit tests.
* Peter Jensen <peter@diff.net>, patch [master f9fee3b]
* Robert Edmonds <edmonds@debian.org>, for Autotools advice and assistance
   interpreting DNS packet captures.
