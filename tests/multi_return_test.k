fn min_max(a: int, b: int) -> int, int
    return a, b
end

let lo, hi = min_max(3, 7)
print(lo)
print(hi)

fn divide(a: int, b: int) -> int, int
    return a / b, a - (a / b) * b
end

let result, remainder = divide(10, 3)
print(result)
print(remainder)
