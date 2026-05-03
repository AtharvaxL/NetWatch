# References & Attribution

All external sources used in the design and implementation of NetWatch.

---

## RFCs and Standards

- **RFC 826** — An Ethernet Address Resolution Protocol (ARP)
  https://datatracker.ietf.org/doc/html/rfc826

- **RFC 792** — Internet Control Message Protocol (ICMP)
  https://datatracker.ietf.org/doc/html/rfc792

- **RFC 768** — User Datagram Protocol (UDP)
  https://datatracker.ietf.org/doc/html/rfc768

- **RFC 791** — Internet Protocol (IPv4)
  https://datatracker.ietf.org/doc/html/rfc791

---

## Libraries Used

- **D3.js v7** — Force-directed graph layout and DOM manipulation
  https://d3js.org | License: ISC

- **Winsock2 (ws2_32)** — Windows Sockets API for UDP/TCP networking
  https://learn.microsoft.com/en-us/windows/win32/winsock/windows-sockets-start-page-2
  (Part of Windows SDK — Microsoft)

---

## Protocol Design References

- **SNMP v2 RFC 1157** — Studied as prior art for network management protocols;
  NWP was designed to be lighter-weight and push-based rather than poll-based.
  https://datatracker.ietf.org/doc/html/rfc1157

- **Wireshark Developer Guide** — Referenced for understanding packet capture
  and dissector design.
  https://www.wireshark.org/docs/wsdg_html_chunked/

---

## Algorithms

- **Z-score anomaly detection** — Statistical outlier detection using rolling mean
  and standard deviation. Concept from:
  https://en.wikipedia.org/wiki/Standard_score

- **D3 Force Simulation** — Node/link force-directed layout:
  https://d3js.org/d3-force

---

## Development Tools

- **MinGW-w64** — GCC port for Windows, used to compile C++17 code
  https://www.mingw-w64.org

- **g++ (MinGW GCC)** — C++ compiler (version 6.3.0+)
  https://gcc.gnu.org

---

*All C++ code in this repository was written by the team. No code was copied from
external sources. Libraries (D3.js, Winsock2) are used via their public APIs only.*
