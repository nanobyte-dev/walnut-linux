#!/bin/sh
# incremental all-gcc rebuild of the port experiment; prints the unique errors
cd /work/build/spu-port/obj && make -k -j24 all-gcc > ../build-inc.log 2>&1
echo "exit $?"
grep -E "^[^ ]*: (error|fatal error)" ../build-inc.log | sort -u | sed 's|.*/gcc-16.2.0/gcc/||' > ../errors-inc.txt
echo "errors: $(wc -l < ../errors-inc.txt)"; sed 's/:.*//' ../errors-inc.txt | sort | uniq -c | sort -rn | head
