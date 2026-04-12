Waf: Entering directory `/home/cs18/tarballs/ns-allinone-3.34/ns-3.34/build'
[2280/2392] Compiling scratch/train4_opt.cc
/home/cs18/tarballs/ns-allinone-3.34/ns-3.34/src/ap-app/wscript:26: Warning: (in /home/cs18/tarballs/ns-allinone-3.34/ns-3.34/src/ap-app) Requested to build modular python bindings, but apidefs dir not found => skipped the bindings.
  bld.ns3_python_bindings()
../scratch/train4_opt.cc: In function ‘int main(int, char**)’:
../scratch/train4_opt.cc:359:1: error: ‘CSMA’ was not declared in this scope
  359 | CSMA/CA
      | ^~~~
../scratch/train4_opt.cc:359:6: error: ‘CA’ was not declared in this scope
  359 | CSMA/CA
      |      ^~
../scratch/train4_opt.cc:420:37: error: ‘wifi’ was not declared in this scope
  420 |     NetDeviceContainer sw1WifiDev = wifi.Install(switchPhy, switchMac, sw1);
      |                                     ^~~~
../scratch/train4_opt.cc:458:37: error: ‘channel’ was not declared in this scope; did you mean ‘channelA’?
  458 |     Ptr<YansWifiChannel> channelA = channel.Create();
      |                                     ^~~~~~~
      |                                     channelA
../scratch/train4_opt.cc:469:5: error: ‘mac’ was not declared in this scope
  469 |     mac.SetType("ns3::StaWifiMac",
      |     ^~~

Waf: Leaving directory `/home/cs18/tarballs/ns-allinone-3.34/ns-3.34/build'
Build failed
 -> task in 'train4_opt' failed with exit status 1 (run with -v to display more information)
