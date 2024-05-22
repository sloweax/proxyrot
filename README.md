## Usage
```
usage: proxyrot [OPTION...]
OPTION:
     -h,--help                      shows usage and exits
     -v,--version                   shows version and exits
     -P,--proxies FILE              add proxies from FILE
     -n,--no-auth                   allow NO AUTH
     -u,--userpass USER:PASS        add USER:PASS
     -p,--port PORT                 listen on PORT (1080 by default)
     -a,--addr ADDR                 bind on ADDR (0.0.0.0 by default)
     -w,--workers WORKERS           number of WORKERS (8 by default)
     -t,--timeout SECONDS           set timeout (10 by default)
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

$ proxyrot -n -P proxies &
listening on 0.0.0.0:1080

$ for i in {1..10}; do curl ifconfig.me -x socks5://127.0.0.1:1080; echo; done
77.77.77.77
11.22.33.44
43.85.12.2
77.77.77.77
11.22.33.44
43.85.12.2
...
```
