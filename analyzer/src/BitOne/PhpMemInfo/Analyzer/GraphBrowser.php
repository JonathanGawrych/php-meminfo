<?php

namespace BitOne\PhpMemInfo\Analyzer;

use Fhaculty\Graph\Graph;
use Graphp\Algorithms\ShortestPath\BreadthFirst;

/**
 * Analyzer to load the data as a graph and analyze the graph.
 *
 * @author    Benoit Jacquemont <benoit.jacquemont@gmail.com>
 * @copyright 2016 Benoit Jacquemont
 * @license   http://opensource.org/licenses/MIT MIT
 */
class GraphBrowser
{
    /** @var array */
    protected $items;

    /** @var Graph */
    protected $graph;

    /**
     * @param array $items
     */
    public function __construct(array $items)
    {
        $this->items = $items;
    }

    /**
     * Find all references coming from roots.
     *
     * @param string $itemid
     *
     * @return array
     */
    public function findReferencePaths($itemId, $output)
    {
        $graph = $this->getGraph($output);

        $to = $graph->getVertex($itemId);

        // $paths = [];

        $mapSearch = new BreadthFirst($to);

        $paths = $mapSearch->getEdgesMap();

        foreach ($map as $endVertexId => $path) {
            // $endVertex = $graph->getVertex($endVertexId);
            // $endVertexData = $endVertex->getAttribute('data');

            // if ($endVertexData['is_root']) {
                $paths[$endVertexId] = $path;
            // }
        }

        if ($to->getAttribute('data')['is_root']) {
            $edge = $to->createEdgeTo($to);
            $edge->setAttribute('name', '<self>');
            $paths[$to->getId()] = [$edge];
        }

        return $paths;
    }

    /**
     * Find all references coming from roots.
     *
     * @param string $itemid
     *
     * @return array
     */
    public function findReferenceParents($itemId, $output)
    {
        $graph = $this->getGraph($output);

        $to = $graph->getVertex($itemId);

        $mapSearch = new BreadthFirst($to);

        return $mapSearch->getParentMap();
    }

    /**
     * Build the graph if necessary and return it.
     *
     * @param Graph
     */
    public function getGraph($output)
    {
        if (null === $this->graph) {
            $this->buildGraph($output);
        }

        return $this->graph;
    }

    /**
     * Build the graph from the items.
     */
    protected function buildGraph($output)
    {
        $this->graph = new Graph();
        $this->createVertices($output);
        $this->createEdges($output);
    }

    /**
     * Create vertices on the graph from items.
     */
    protected function createVertices($output)
    {
        $output->writeln('Processing ' . count($this->items) . ' vertices');
        $num = 0;
        foreach ($this->items as $itemId => $itemData) {
            if (++$num % 50000 === 0) {
                $output->writeln('Processed ' . $num . ' vertices');
            }
            $vertex = $this->graph->createVertex($itemId);
            $vertex->setAttribute('data', $itemData);
        }
    }

    /**
     * Create edges on the graph between vertices.
     */
    protected function createEdges($output)
    {
        $output->writeln('Processing ' . count($this->items) . ' vertices for edges');
        $num = 0;
        foreach ($this->items as $itemId => $itemData) {
            if (++$num % 10000 === 0) {
                $output->writeln('Processed ' . $num . ' vertices for edges');
            }
            if (isset($itemData['children'])) {
                $children = $itemData['children'];
                $vertex = $this->graph->getVertex($itemId);

                foreach ($children as $link => $child) {
                    $childVertex = $this->graph->getVertex($child);
                    $childVertex->createEdgeTo($vertex)->setAttribute('name', $link);
                }
            }
        }
    }
}
