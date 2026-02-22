# negative numbers
let a: int = -5
let b: int = -10
print(a)
print(b)

# and / or
let x: int = 1
let y: int = 0
if x == 1 and y == 0
    print(1)
end
if x == 0 or y == 0
    print(1)
end

# strlen
let s: str = "hello"
print(strlen(s))

# break
for i = 0 to 10
    if i == 5
        break
    end
    print(i)
end

# continue
for j = 0 to 5
    if j == 3
        continue
    end
    print(j)
end

# do while
let n: int = 0
do
    print(n)
    n = n + 1
while n < 3
