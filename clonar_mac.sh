#!/bin/sh

# Clone Mac Address
# Facundo M. de la Cruz
# Use: clonemac <NIC> 

if [ -z $1 ]; then 
   printf "Use: clonemac <NIC>\n\n"
   exit 1
fi

while true; do
   ifconfig $1 hw ether $MAC > /dev/null 2>&1
   if [ $? == '0' ]; then 
         ifconfig $1 | grep HWaddr | awk {'print "New " $4 ": " $5'}
         break
   else
         MAC=`(date; cat /proc/interrupts) | md5sum |
         sed -r 's/^(.{10}).*$/\1/;s/([0-9a-f]{2})/\1:/g; s/(.*)
         :$/00:\1/;' `
   fi
done
exit 0
