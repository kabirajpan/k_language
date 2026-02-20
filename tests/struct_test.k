struct Point
    x: int
    y: int
end

struct Player
    x: int
    y: int
    health: int
end

# create struct
let p = Point(10, 20)
print(p.x)
print(p.y)

# mutate field
p.x = 99
print(p.x)

# second struct
let hero = Player(5, 8, 100)
print(hero.x)
print(hero.y)
print(hero.health)

# mutate
hero.health = 75
print(hero.health)
