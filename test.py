x = 5
print(type(x))

def a():
    global x
    x = 6
    y = 1
    def b():
        nonlocal y
        y = 10