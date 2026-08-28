#!/usr/bin/awk -f
BEGIN { 
 first = 1
 t2Col = ""
 ogCol = ""
 count = 0
}

/\{.*,.*\}/ {
 gsub(/[{} \t]/, "")
 gsub(/,$/, "")
 n = split($0, pair, ",")
 if (n >= 2) {
   if (!first) {
     t2Col = t2Col ", "
     ogCol = ogCol ", "
   }
   t2Col = t2Col pair[1]
   ogCol = ogCol pair[2]
   first = 0
   count++
 }
}

END {
  print "const unsigned int _t2ImgTable[" count "] __attribute__((aligned(64))) = {" t2Col "};"
  print "const unsigned int _ogImgTable[" count "] __attribute__((aligned(64))) = {" ogCol "};"
  print "const unsigned int* const t2ImgTable __attribute__((aligned(64))) = _t2ImgTable;"
  print "const unsigned int* const ogImgTable __attribute__((aligned(64))) = _ogImgTable;"
  print "unsigned int* meCoreSystemTable __attribute__((aligned(64))) = (unsigned int*)t2ImgTable;"
}
