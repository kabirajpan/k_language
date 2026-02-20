# inline initialiser
let nums: int[5] = {10, 20, 30, 40, 50}

print(nums[0])
print(nums[2])
print(nums[4])

# loop over it
for i = 0 to 4
    print(nums[i])
end

# old style still works
let other: int[3]
other[0] = 100
other[1] = 200
other[2] = 300
print(other[1])
