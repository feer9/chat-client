Generate .res files with:

Qt\Tools\mingw730_64\bin\windres D:\git\chat-client\resources\versioninfo.rc -O coff -o D:\git\chat-client\resources\versioninfo.res
Qt\Tools\mingw730_64\bin\windres D:\git\chat-client\resources\icon.rc -O coff -o D:\git\chat-client\resources\icon.res

Then link with "icon.res" "versioninfo.res" arguments as input files
