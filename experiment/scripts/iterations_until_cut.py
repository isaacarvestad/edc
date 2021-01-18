#! /usr/bin/env python3

from math import log10
import itertools
import sys
import multiprocessing as mp
import subprocess
import csv
import numpy


def cut(edc_cut_path, graph_info, phi, random_walk_steps):
    """Run 'edc-cut', assert balanced cut is returned, and return the graph
    parameters, phi, and the number of iterations run.

    """
    graph_string, graph_params, graph_edges = graph_info
    result = subprocess.run([edc_cut_path, f'-phi={phi}', f'-random_walk_steps={random_walk_steps}'],
                            input=graph_string,
                            text=True,
                            check=True,
                            timeout=480,
                            stdout=subprocess.PIPE)
    if result.returncode != 0:
        print(f'Failed cut: {result.stdout}')
        exit(1)
    else:
        lines = result.stdout.split('\n')
        resultType = lines[0].split()[0]
        iterations = int(lines[0].split()[1])
        if resultType != 'balanced_cut':
            print(f'Cut did not result in balanced_cut: {resultType}')
            exit(1)
        xlen, *xs = list(map(int, lines[1].split()))
        assert (xlen == len(xs))
        ylen, *ys = list(map(int, lines[2].split()))
        assert (ylen == len(ys))

        assert(max(xs) < min(ys) or max(ys) < min(xs))

        return (graph_params, phi, random_walk_steps, graph_edges, iterations)


if __name__ == '__main__':
    if len(sys.argv) != 6:
        print('Expected five arguments')
        exit(1)
    _, _, edc_cut_path, seed, gen_graph, output_file = sys.argv

    graph_params = [{
        'name': 'clique',
        'n': 30,
        'k': 2,
        'r': 1,
        'p': 100,
    }, {
        'name': 'clique',
        'n': 100,
        'k': 2,
        'r': 1,
        'p': 100,
    }, {
        'name': 'clique',
        'n': 200,
        'k': 2,
        'r': 1,
        'p': 100,
    }, {
        'name': 'clique',
        'n': 500,
        'k': 2,
        'r': 1,
        'p': 100,
    }, {
        'name': 'margulis',
        'n': 10,
        'k': 2,
        'r': 1,
    }, {
        'name': 'margulis',
        'n': 50,
        'k': 2,
        'r': 1,
    }, {
        'name': 'margulis',
        'n': 100,
        'k': 2,
        'r': 1,
    }, {
        'name': 'margulis',
        'n': 150,
        'k': 2,
        'r': 1,
    }]

    def graphParamsToString(p):
        ps = [p['name'], str(p['n']), str(p['k']), str(p['r'])]
        if 'p' in p:
            ps.append(str(p['p']))
        return '-'.join(ps)

    # contains tuples (graph_string, parameters for generating graph, edges in graph)
    graphs = []
    for ps in graph_params:
        cmd = [
            f'./{gen_graph}', f'--seed={seed}', ps['name'],
            '-n={}'.format(ps['n']), '-k={}'.format(ps['k']),
            '-r={}'.format(ps['r'])
        ]
        result = subprocess.run(cmd,
                                text=True,
                                check=True,
                                timeout=60,
                                stdout=subprocess.PIPE)
        if result.returncode != 0:
            print('Generating graph with type {} failed'.format(ps['name']))
            exit(1)
        lines = result.stdout.strip().split('\n')
        m = int(lines[0].split()[1])
        assert (len(lines) == m + 1)
        graphs.append((result.stdout, ps, m))

    with mp.Pool() as pool:
        phis = [0.001]
        random_walk_stepss = [1,10,100]
        numIterations = 8
        jobs = [(edc_cut_path, graph_info, phi, random_walk_steps)
                for graph_info, phi, random_walk_steps, _ in itertools.product(
                        graphs, phis, random_walk_stepss, range(numIterations))]
        result = pool.starmap(cut, jobs, chunksize=1)

    with open(output_file, 'w') as f:
        writer = csv.DictWriter(f,
                                fieldnames=[
                                    'graph',
                                    'phi',
                                    'random_walk_steps',
                                    'log10_squared_edges',
                                    'iterations',
                                ])
        writer.writeheader()

        for p, phi, random_walk_steps, edges, iterations in result:
            writer.writerow({
                'graph': graphParamsToString(p),
                'phi': phi,
                'random_walk_steps': random_walk_steps,
                'log10_squared_edges': log10(edges) * log10(edges),
                'iterations': iterations,
            })
