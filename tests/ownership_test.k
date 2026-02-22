fn process()
    let buf: ptr = alloc(1024)
    deref(buf) = 42
    print(deref(buf))
    # no free needed — compiler handles it
end

process()
print(99)
