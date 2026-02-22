# for with condition — print even numbers
print("even numbers:")
for i = 0 to 10 if i - (i / 2) * 2 == 0
    print(i)
end

# do while — runs at least once
print("do while:")
let x: int = 0
do
    print(x)
    x = x + 1
while x < 5
