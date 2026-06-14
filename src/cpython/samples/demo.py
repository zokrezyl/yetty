import os
from math import sqrt as root


def greet(name: str, *, loud=False) -> str:
    msg = f"hello {name}"
    return msg.upper() if loud else msg


class Point:
    x: int = 0

    def dist(self):
        return root(self.x ** 2)


data = [n for n in range(10) if n % 2 == 0]

match data:
    case [first, *rest]:
        print(first, rest)
