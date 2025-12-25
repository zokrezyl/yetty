When using the ymery plugin and exiting the terminal it coredumps.

Likely the cleanup logic is wrong


```
Thread 1 "yetty" received signal SIGSEGV, Segmentation fault.
0x00007ffff7b2b45e in std::_Sp_counted_ptr<ymery::WidgetFactory*, (__gnu_cxx::_Lock_policy)2>::_M_dispose() ()
   from /home/user/work/my/yetty-poc/build/libymery.so
(gdb) bt
#0  0x00007ffff7b2b45e in std::_Sp_counted_ptr<ymery::WidgetFactory*, (__gnu_cxx::_Lock_policy)2>::_M_dispose() ()
   from /home/user/work/my/yetty-poc/build/libymery.so
#1  0x00005555555d0d1e in std::_Sp_counted_base<(__gnu_cxx::_Lock_policy)2>::_M_release_last_use_cold() ()
#2  0x00007ffff7b3a574 in std::_Sp_counted_ptr<ymery::EmbeddedApp*, (__gnu_cxx::_Lock_policy)2>::_M_dispose() ()
   from /home/user/work/my/yetty-poc/build/libymery.so
#3  0x00005555555d93d2 in yetty::Ymery::dispose() ()
#4  0x00005555555c527f in yetty::PluginManager::~PluginManager() ()
#5  0x00005555555740a7 in main ()
```
