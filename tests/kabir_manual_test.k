

let nums: int[5] = {1, 2, 3, 2, 1}
let isPalindrome: bool = true


let first: int = 0
let last:  int = 4

while first < last
  if nums[first] != nums[last]
    isPalindrome = false
    break
  end

  first = first + 1
  last = last - 1
end

print("is Palindrome")
print(isPalindrome)



