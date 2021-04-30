# bouncer

[![Build](https://github.com/sjinks/bouncer/actions/workflows/test.yml/badge.svg)](https://github.com/sjinks/bouncer/actions/workflows/test.yml)
[![Coverity Scan Build Status](https://scan.coverity.com/projects/3116/badge.svg)](https://scan.coverity.com/projects/3116)

epoll()-based SMTP server that bounces incoming connections

## Usage

One of the usages of `bouncer` is a redirection of the traffic to known email trap servers to `bouncer`.
For example, if you know that email traps have IPs 1.2.3.4, 5.6.7.8, and 9.10.11.12, you can do something like this:

```bash
setsid ./bouncer
```

```bash
for i in 1.2.3.4 5.6.7.8 9.10.11.12; do
    iptables -t nat -A OUTPUT -p tcp -d $i --dport 25 -j DNAT --to-destination 127.0.0.1:10025
done
```

Then, when your mail server attempts to connect to a trap, the connection will be redirected to `bouncer`:

```
$ telnet 1.2.3.4 smtp
Trying 1.2.3.4...
Connected to 1.2.3.4.
Escape character is '^]'.
554 5.3.2 HELLO FROM THE BOUNCER!
QUIT
221 2.0.0 Bye.
Connection closed by foreign host.
```

When analyzing the mail server logs, you should suppress all recipients who generated such a bounce.

This is useful for ESPs who mail on behalf of their clients: when you get to know your client is a spammer, the damage has been done, and your IP addresses have a bad reputation or even blacklisted.
By using `bouncer` to suppress all known trap domains, you have a chance to detect spammers before your IPs are blacklisted and take action against them.
