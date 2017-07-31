A stock viewer and selection tool evolved from top.

Depends on the ncurses lib.

The 'data' directory contains the transaction deal respectively.

Open a terminal to buid:
   make 

Open a terminal to execute:
   ./stocktop 

Four tab is supported so far just like top.

The following commmand is suppored:
  ****************************************************************************
  Z,B,E,e   Global: 'Z' colors; 'B' bold; 'E'/'e' summary/task memory scale
  
  l,t,m     Toggle Summary: 'l' load avg; 't' task/cpu stats; 'm' memory info
  
  0,1,2,3,I Toggle: '0' zeros; '1/2/3' cpus or numa node views; 'I' Irix mode
  
  f,F,X     Fields: 'f'/'F' add/remove/order/sort; 'X' increase fixed-width
  ****************************************************************************  
  L,&,<,> . Locate: 'L'/'&' find/again; Move sort column: '<'/'>' left/right
  
  R,H,V,J . Toggle: 'R' Sort; 'H' Threads; 'V' Forest view; 'J' Num justify
  
  c,i,S,j . Toggle: 'c' Cmd name/line; 'i' Idle; 'S' Time; 'j' Str justify
  
  x,y     . Toggle highlights: 'x' sort field; 'y' running tasks
  
  z,b     . Toggle: 'z' color/mono; 'b' bold/reverse (only if 'x' or 'y')
  
  u,U,o,O,= Filter by: 'u'/'U' effective/any user; 'o'/'O' other criteria; '=' cancel criteria
  
  n,#,^O  . Set: 'n'/'#' max tasks displayed; Show: Ctrl+'O' other filter(s)
  
  C,...   . Toggle scroll coordinates msg for: up,down,left,right,home,end
  ****************************************************************************
  k,r       Manipulate tasks: 'k' kill; 'r' renice
  
  d or s    Set update interval
  
  W,Y       Write configuration file 'W'; Inspect other output 'Y'
  
  q         Quit
  
          ( commands shown with '.' require a visible task display window ) 
          
Press 'h' or '?' for help with Windows,
