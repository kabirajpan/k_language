# bool literals
let flag: bool = true
let done: bool = false
print(flag)
print(done)

# bool from comparison
let x: int = 10
let is_big: bool = x > 5
print(is_big)

# bool in if
if flag
    print("flag is true")
end

# bool in while
let count: int = 0
let running: bool = true
while running
    count = count + 1
    if count == 3
        running = false
    end
end
print(count)
