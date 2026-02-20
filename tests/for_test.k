# tests/for_test.k

# basic for loop (step defaults to 1)
for i = 1 to 5
    print(i)
end

# for loop with step 2
for j = 0 to 10 step 2
    print(j)
end

# nested for loop
for x = 1 to 3
    for y = 1 to 3
        print(x * y)
    end
end
