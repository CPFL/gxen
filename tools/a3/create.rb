0.upto(ARGV.first.to_i) {|i|
    if (i + 1) % 4 == 0
        sleep 20
    end
    system " xl create ~/dev/vm/evaluations/ubuntu#{i + 1}.hvm"
}
