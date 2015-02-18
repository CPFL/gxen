0.upto(ARGV.first.to_i) {|i|
    system "xl destroy vm#{i + 1}"
}
