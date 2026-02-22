let nums: int[8] = {64, 34, 25, 12, 22, 11, 90, 1}

print("before:")
for i = 0 to 7
    print(nums[i])
end

let outer: int = 0
while outer < 8
    let inner: int = 0
    while inner < 7
        let a: int = nums[inner]
        let b: int = nums[inner + 1]
        if a > b
            let tmp: int = a
            nums[inner] = b
            nums[inner + 1] = tmp
        end
        inner = inner + 1
    end
    outer = outer + 1
end

print("after:")
for k = 0 to 7
    print(nums[k])
end
