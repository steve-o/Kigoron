Static symbology interactive provider in UPA, snapshot/fake-snapshot only.

usage:

```bash
	kigoron.exe --symbol-path=nsq.csv,nyq.csv
```

tbd:

 * http/snmp admin interface.
 * stale group id per file on configured expiration time.
 * histogram and performance counter instrumentation.
 * cool connectivity logging for clients.
 * rdn_exchid support with enumerated dictionary.

by design:

 * no support for windows performance counters.
 * no support for windows management console or powershell.

example consumption:

```bash
rsslConsumer -h 127.0.0.1 -p 24002 -s NOCACHE_VTA -mp ISIN=US0378331005
Proxy host:
Proxy port:

Input arguments...

Using Connection Type = 0
srvrHostname: 127.0.0.1
srvrPortNo: 24002
serviceName: NOCACHE_VTA

Attempting to connect to server 127.0.0.1:24002...

Attempting to connect to server 127.0.0.1:24002...

Channel IPC descriptor = 124

Channel 124 In Progress...

Channel 124 Is Active
Connected to upa8.0.0.L1.win.rrg 64-bit Static device.
Ping Timeout = 60

Received Login Response for Username: reutadmin
        State: Open/Ok/None - text: ""


Received Source Directory Response
        State: Open/Ok/None - text: ""

Received serviceName: NOCACHE_VTA


ISIN=US0378331005
DOMAIN: RSSL_DMT_MARKET_PRICE
State: Non-streaming/Suspect/None - text: ""
        PRIM_RIC            AAPL.O
        CLASS_CODE          EQU
        EXCH_SNAME          NSQ
        CCY_NAME            USD
        DSPLY_NAME          APPLE INC
        ISIN_CODE           US0378331005
        CUSIP_CD            037833100
        SEDOL               2046251
        GICS_CODE           45202030
        VALUE_TS1           02:24:17:000:000:000
        VALUE_DT1           12 JUL 2015
```
