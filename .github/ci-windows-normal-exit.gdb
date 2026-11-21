set pagination off
set confirm off
set print thread-events off
set print frame-arguments none
set print address off
run
if $_isvoid($_exitcode)
  echo Wallet observation stopped before process exit\n
  echo Wallet frame names begin\n
  backtrace 18
  echo Wallet frame names end\n
else
  printf "Wallet inferior exit code: %u\n", (unsigned int)$_exitcode
end
