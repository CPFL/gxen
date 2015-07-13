0.upto(ARGV.first.to_i) {|i|
    system "xl shutdown vm#{i + 1}"
}
