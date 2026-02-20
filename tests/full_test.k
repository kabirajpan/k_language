# types
let x: int = 10
let name: str = "kabir"
let flag: bool = true
let nums: int[3]

# array
nums[0] = 100
nums[1] = 200
nums[2] = 300

# logic
if flag
    print(name)
end

# loop + array
for i = 0 to 2
    print(nums[i])
end

# function
fn double(n: int) -> int
    return n * 2
end

print(double(x))
