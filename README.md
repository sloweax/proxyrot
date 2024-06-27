## Usage
```
usage: proxyrot [OPTION...]
OPTION:
     -h,--help                      shows usage and exits
     -v,--version                   shows version and exits
     -P,--proxies FILE              add proxies from FILE
     -n,--no-auth                   allow no auth authentication
     -u,--userpass USER:PASS        use USER:PASS as authentication
     -p,--port PORT                 listen on PORT (1080 by default)
     -a,--addr ADDR                 bind on ADDR (127.0.0.1 by default)
     -w,--workers WORKERS           number of WORKERS (8 by default)
     -t,--timeout SECONDS           set connection timeout (10 by default)
     -r,--retry                     if proxy connection fail, try another
```

## Build
Make sure you have `gcc` and `make` installed
```
git clone https://github.com/sloweax/proxyrot
cd proxyrot
make install
```

## Example
```
$ cat proxies
# protocol host port [options]
socks5 77.77.77.77 9050
socks5 11.22.33.44 123 user pass
socks5 proxy.com 1080 user
# you can also chain proxies
socks5 1.2.3.4 user pass | socks5 4.3.2.1

$ proxyrot -n -P proxies &
listening on 0.0.0.0:1080

$ for i in {1..10}; do curl ifconfig.me -x socks5://127.0.0.1:1080; echo; done
77.77.77.77
11.22.33.44
43.85.12.2
4.3.2.1
77.77.77.77
11.22.33.44
43.85.12.2
4.3.2.1
...
```
