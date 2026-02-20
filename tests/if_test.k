# tests/if_test.k
# tests else, elif, and all new comparison operators

let x = 10

# test else
if x > 5
    print(1)
else
    print(0)
end

# test elif
if x == 5
    print(100)
elif x == 10
    print(200)
else
    print(300)
end

# test !=
if x != 99
    print(1)
end

# test >=
if x >= 10
    print(1)
end

# test <=
if x <= 10
    print(1)
end
