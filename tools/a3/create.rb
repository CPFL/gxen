0.upto(ARGV.first.to_i) {|i|
    system " xl create ~/dev/vm/evaluations/ubuntu#{i + 1}.hvm"
}
