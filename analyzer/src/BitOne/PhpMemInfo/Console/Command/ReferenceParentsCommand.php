<?php

namespace BitOne\PhpMemInfo\Console\Command;

use BitOne\PhpMemInfo\Analyzer\GraphBrowser;
use BitOne\PhpMemInfo\Loader;
use Fhaculty\Graph\Vertex;
use Symfony\Component\Console\Command\Command;
use Symfony\Component\Console\Input\InputArgument;
use Symfony\Component\Console\Input\InputInterface;
use Symfony\Component\Console\Output\OutputInterface;

/**
 * Command to find reference parents.
 *
 * @author    Benoit Jacquemont <benoit.jacquemont@gmail.com>
 * @copyright 2016 Benoit Jacquemont
 * @license   http://opensource.org/licenses/MIT MIT
 */
class ReferenceParentsCommand extends Command
{
    private $graph;
    private $output;
    private $seenNodes;
    
    /**
     * {@inheritedDoc}.
     */
    protected function configure()
    {
        $this
            ->setName('ref-parents')
            ->setDescription('Find reference parents to an item')
            ->addArgument(
                'item-id',
                InputArgument::REQUIRED,
                'Item Id in 0xaaaaaaaa format'
            )
            ->addArgument(
                'dump-file',
                InputArgument::REQUIRED,
                'PHP Meminfo Dump File in JSON format'
            );
    }

    /**
     * {@inheritdoc}
     */
    protected function execute(InputInterface $input, OutputInterface $output)
    {
        $this->output = $output;
        $dumpFilename = $input->getArgument('dump-file');
        $itemId = $input->getArgument('item-id');

        $loader = new Loader();

        $items = $loader->load($dumpFilename);

        $graphBrowser = new GraphBrowser($items);
        $this->graph = $graphBrowser->getGraph($output);

        $parentsMap = $graphBrowser->findReferenceParents($itemId, $output);

        $output->writeln(sprintf('<info>Found %d generations</info>', count($parentsMap)));
        $this->seenNodes = [];
        foreach ($parentsMap as $child => $parents) {
            $childVertex = $this->graph->getVertex($child);
            $this->output->writeln('');
            $this->output->writeln('');
            $this->outputVertex($childVertex, 0);
            foreach ($parents as $parent) {
                $parentVertex = $this->graph->getVertex($parent);
                $edge = $childVertex->getEdgesTo($parentVertex)->getEdgeFirst();
                $this->output->writeln('');
                $output->write($edge->getAttribute('name') . ' -> ');
                $this->output->writeln('');
                $this->outputVertex($parentVertex, 1);
            }
        }
        
        // $this->recurseParents($itemId, $parentsMap);

        return 0;
    }
    
    private function recurseParents($nodeId, $parentsMap, $level = 0) {
        if (isset($this->seenNodes[$nodeId])) {
            return;
        }
        
        $this->outputVertex($this->graph->getVertex($nodeId), $level);
        $this->seenNodes[$nodeId] = true;
        
        foreach ($parentsMap[$nodeId] as $parent) {
            $this->recurseParents($parent, $parentsMap, $level + 1);
        }
    }

    /**
     * Prepare an array with a vertex data.
     *
     * @param Vertex $vertex
     *
     * @return array
     */
    protected function outputVertex(Vertex $vertex, $indent)
    {
        $vertexData = $vertex->getAttribute('data');
        $this->output->write(str_repeat('    ', $indent));
        $formatter = $this->getHelper('formatter');
        $this->output->write($vertex->getId() . ' (' . $formatter->formatMemory($vertexData['size']) . '): ');
        $this->output->write($vertexData['type']);
        if ($vertexData['type'] === 'object') {
            $this->output->write(' [' . $vertexData['class'] . ']');
        }
    }
}
