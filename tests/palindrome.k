# check if a number is palindrome
# e.g. 121 -> yes, 123 -> no

fn is_palindrome(n: int) -> int
    let original: int = n
    let reversed: int = 0
    while n > 0
        let digit: int = n - (n / 10) * 10
        reversed = reversed * 10 + digit
        n = n / 10
    end
    if original == reversed
        return 1
    end
    return 0
end

let result: int = is_palindrome(121)
print(result)

result = is_palindrome(123)
print(result)

result = is_palindrome(1221)
print(result)

result = is_palindrome(12321)
print(result)
