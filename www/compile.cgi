#!/bin/sh
echo "Content-type:text/html"
echo ""
echo "<html>"
echo "<p>"
echo "User:"$(whoami)
echo "COMPILING..."
echo $(gcc www/hello.c -o www/hello.cgi)
#Why using www?
#Because it's default directory is where httpd there,not CGI.
echo "<a href='hello.cgi'>Visit</a>"
echo "</p>"
echo "</html>"
