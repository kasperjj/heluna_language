#!/usr/bin/env python3
"""Deterministic benchmark data generator for the Heluna VM benchmark suite.

Generates JSON files of varying sizes with a fixed random seed for reproducibility.
Each run produces identical output, ensuring benchmark results are comparable.
"""

import json
import random
import argparse
import os

SEED = 42

FIRST_NAMES = [
    "Alice", "Bob", "Charlie", "Diana", "Eve", "Frank", "Grace", "Hank",
    "Ivy", "Jack", "Karen", "Leo", "Mona", "Nick", "Olivia", "Paul",
    "Quinn", "Rita", "Sam", "Tina", "Uma", "Victor", "Wendy", "Xander",
    "Yara", "Zane", "Ada", "Ben", "Cora", "Dan", "Ella", "Finn",
    "Gina", "Hugo", "Iris", "Joel", "Kara", "Luke", "Maya", "Noel",
    "Opal", "Pete", "Rose", "Seth", "Tara", "Uri", "Vera", "Wade",
    "Xena", "Yuri"
]

LAST_NAMES = [
    "Johnson", "Smith", "Williams", "Brown", "Jones", "Garcia", "Miller",
    "Davis", "Rodriguez", "Martinez", "Hernandez", "Lopez", "Gonzalez",
    "Wilson", "Anderson", "Thomas", "Taylor", "Moore", "Jackson", "Martin",
    "Lee", "Perez", "Thompson", "White", "Harris", "Sanchez", "Clark",
    "Ramirez", "Lewis", "Robinson", "Walker", "Young", "Allen", "King",
    "Wright", "Scott", "Torres", "Nguyen", "Hill", "Flores", "Green",
    "Adams", "Nelson", "Baker", "Hall", "Rivera", "Campbell", "Mitchell",
    "Carter", "Roberts"
]

DOMAINS = [
    "techcorp.com", "megasoft.io", "dataflow.net", "cloudnine.org",
    "appworks.dev", "bytewise.co", "netprime.com", "codelab.io",
    "digihub.net", "webforge.com"
]

DEPARTMENTS = [
    "engineering", "marketing", "sales", "finance",
    "operations", "research", "support", "design"
]


def generate_record(rng, record_id):
    first = rng.choice(FIRST_NAMES)
    last = rng.choice(LAST_NAMES)
    domain = rng.choice(DOMAINS)
    dept = rng.choice(DEPARTMENTS)
    age = rng.randint(18, 75)
    score = round(rng.uniform(0.0, 100.0), 2)
    salary = round(rng.uniform(30000.0, 150000.0), 2)
    active = rng.random() < 0.8

    # Add leading/trailing whitespace to name and email (benchmarks trim work)
    name = f"  {first} {last}  "
    email = f"  {first}.{last.upper()}@{domain}  "

    return {
        "id": record_id,
        "name": name,
        "email": email,
        "age": age,
        "score": score,
        "active": active,
        "department": dept,
        "salary": salary
    }


def generate_dataset(count):
    rng = random.Random(SEED)
    records = [generate_record(rng, i + 1) for i in range(count)]
    return {"records": records}


SIZES = {
    "tiny": 1,
    "small": 100,
    "medium": 10000,
    "large": 100000,
}


def main():
    parser = argparse.ArgumentParser(description="Generate Heluna benchmark data")
    parser.add_argument("--output-dir", default=".", help="Output directory (default: current directory)")
    args = parser.parse_args()

    output_dir = args.output_dir
    os.makedirs(output_dir, exist_ok=True)

    for name, count in SIZES.items():
        path = os.path.join(output_dir, f"{name}.json")
        data = generate_dataset(count)
        with open(path, "w") as f:
            json.dump(data, f, separators=(",", ":"))
        size_kb = os.path.getsize(path) / 1024
        print(f"Generated {path}: {count} records ({size_kb:.1f} KB)")


if __name__ == "__main__":
    main()
