```
=========== You should know that some source code in this project is
= WARNING = not a best practice and immature.
===========                                            -- 2015/10/28
```

This project is a fork of [Xiaochen Wang's tapip project](https://github.com/chobits/tapip).

My hackings include:

* I added a command `attach_dev` in the interactive shell, you can use this `tapip` program to control a real ethernet port.


tapip
=====
A user-mode TCP/IP stack based on linux tap device

Dependence
==========
* linux tun/tap device (/dev/net/tun exists if supported)
* pthreads

Quick start
===========
```
$ make
# ./tapip                    (run as root)
[net shell]: ping 10.0.0.2   (ping linux kernel TCP/IP stack)
(waiting icmp echo reply...)
[net shell]: help            (see all embedded commands)
...
```
The corresponding network topology is [here](doc/net_topology#L126).

More information for hacking TCP/UDP/IP/ICMP:  
  See [doc/net_topology](doc/net_topology), and select a script from [doc/test](doc/test) to do.

Socket Api
==========
_socket,_read,_write,_send,_recv,_connect,_bind,_close and _listen are provided now.  
Three socket types(SOCK_STREAM, SOCK_DGRAM, SOCK_RAW) can be used.  
You can use these apis to write applications working on tapip.  
Good examples are app/ping.c and app/snc.c.

How to implement
================
I refer to xinu and Linux 2.6.11 TCP/IP stack and use linux tap device to simulate net device(for l2).  
A small shell is embedded in tapip.  
So this is just user-mode TCP/IP stack :)

Any use
=======  
Tapip makes it easy to hack TCP/IP stack, just compiling it to run.  
It can also do some network test: arp snooping, proxy, and NAT!  
I think the most important is that tapip helps me learn TCP/IP, and understand linux TCP/IP stack.

Other
=====
You can refer to [saminiir/level-ip](https://github.com/saminiir/level-ip) and its blog posts for more details.
